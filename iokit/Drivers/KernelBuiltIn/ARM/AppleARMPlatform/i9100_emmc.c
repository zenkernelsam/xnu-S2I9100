/* SPDX-License-Identifier: BSD-2-Clause
 * Original read-only MMC/32-bit MSHCI bring-up, not the SD ACMD protocol.
 * Clock/power/pinmux must be established and independently verified by the
 * board owner. Never writes a sector, a card configuration byte or a key. */
#include "i9100_emmc.h"
#include <string.h>
#define C_START (1U<<31)
#define C_RESP (1U<<6)
#define C_LONG (1U<<7)
#define C_CRC (1U<<8)
#define C_DATA (1U<<9)
#define C_WAIT (1U<<13)
#define C_STOP (1U<<14)
#define C_INIT (1U<<15)
#define C_CLOCK (1U<<21)
#define I_CMD_DONE (1U<<2)
#define I_DATA_DONE (1U<<3)
#define I_RESPONSE_CRC (1U<<6)
#define I_DATA_CRC (1U<<7)
#define I_TIMEOUT ((1U<<8)|(1U<<9)|(1U<<10))
#define I_OTHER ((1U<<1)|(1U<<11)|(1U<<12)|(1U<<13)|(1U<<15))
#define I_ERRORS (I_RESPONSE_CRC|I_DATA_CRC|I_TIMEOUT|I_OTHER)
#define R1_ERRORS 0xffffa088U
#define DATA_BUSY (1U<<9)
#define FIFO_WORDS(v) (((v)>>17)&0x1fffU)
#define RESET_BITS 7U
#define CTRL_DMA ((1U<<5)|(1U<<25))
static uint32_t rd(struct i9100_emmc_card *c,unsigned r){return c->io.read32(c->io.cookie,r);}
static void wr(struct i9100_emmc_card *c,unsigned r,uint32_t v){c->io.write32(c->io.cookie,r,v);}
static int delay(struct i9100_emmc_card *c){return c->io.delay_us(c->io.cookie,10U)?I9100_EMMC_IO:0;}
static int interrupt_error(struct i9100_emmc_card *c,uint32_t v)
{
 c->last_rint=v;
 if(v&I_RESPONSE_CRC)return I9100_EMMC_RESPONSE_CRC;
 if(v&I_DATA_CRC)return I9100_EMMC_DATA_CRC;
 if(v&I_TIMEOUT)return I9100_EMMC_TIMEOUT;
 return (v&I_OTHER)?I9100_EMMC_IO:0;
}
static int wait_clear(struct i9100_emmc_card *c,unsigned reg,uint32_t mask,uint64_t ns)
{
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(uint32_t i=0;i<c->io.poll_bound;i++){
  uint32_t v=rd(c,reg);c->last_status=v;
  if(!(v&mask))return 0;
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(now<start || now-start>=ns)return I9100_EMMC_TIMEOUT;
  if(delay(c))return I9100_EMMC_IO;
 }
 return I9100_EMMC_TIMEOUT;
}
static int clock_update(struct i9100_emmc_card *c)
{
 int e=wait_clear(c,EM_CMD,C_START,100000000ULL);if(e)return e;
 wr(c,EM_RINTSTS,I_ERRORS);wr(c,EM_CMDARG,0);
 wr(c,EM_CMD,C_START|C_CLOCK|C_WAIT);
 e=wait_clear(c,EM_CMD,C_START,100000000ULL);
 if(e)return e;
 return interrupt_error(c,rd(c,EM_RINTSTS));
}
static int clock_set(struct i9100_emmc_card *c,uint32_t target)
{
 if(!target || !c->io.input_clock_hz)return I9100_EMMC_CLOCK;
 uint64_t div=((uint64_t)c->io.input_clock_hz+2ULL*target-1)/(2ULL*target);
 if(!div)div=1;
 if(div>255 || c->io.input_clock_hz/(2U*(uint32_t)div)>target)return I9100_EMMC_CLOCK;
 wr(c,EM_CLKENA,0);int e=clock_update(c);if(e)return e;
 wr(c,EM_CLKSRC,0);wr(c,EM_CLKDIV,(uint32_t)div);
 e=clock_update(c);if(e)return e;
 wr(c,EM_CLKENA,1);e=clock_update(c);if(e)return e;
 c->clock_div=(uint32_t)div;c->clock_hz=c->io.input_clock_hz/(2U*c->clock_div);return 0;
}
static int command(struct i9100_emmc_card *c,unsigned cmd,uint32_t arg,uint32_t flags)
{
 /* No general/raw-command export. This list excludes SWITCH, write,
  * erase, trim and all boot/RPMB operations even in error recovery. */
 if(cmd!=0&&cmd!=1&&cmd!=2&&cmd!=3&&cmd!=7&&cmd!=8&&cmd!=9&&cmd!=12&&cmd!=13&&cmd!=17&&cmd!=18)
  return I9100_EMMC_ARGUMENT;
 int e=wait_clear(c,EM_CMD,C_START,100000000ULL);if(e)return e;
 c->last_command=cmd;wr(c,EM_RINTSTS,0xffffffffU);wr(c,EM_CMDARG,arg);
 wr(c,EM_CMD,C_START|flags|cmd);
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(uint32_t i=0;i<c->io.poll_bound;i++){
  uint32_t st=rd(c,EM_RINTSTS);e=interrupt_error(c,st);if(e)return e;
  if(st&I_CMD_DONE){
   c->last_response=rd(c,EM_RESP0);wr(c,EM_RINTSTS,I_CMD_DONE);
   return 0; /* DATA_DONE must remain visible to the data reader. */
  }
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(now<start||now-start>=100000000ULL)return I9100_EMMC_TIMEOUT;
  if(delay(c))return I9100_EMMC_IO;
 }
 return I9100_EMMC_TIMEOUT;
}
static int r1(struct i9100_emmc_card *c){return (c->last_response&R1_ERRORS)?I9100_EMMC_CARD_ERROR:0;}
static int ready_transfer(struct i9100_emmc_card *c)
{
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(unsigned i=0;i<1000;i++){
  int e=command(c,13,c->rca<<16,C_RESP|C_CRC);if(e)return e;
  if(r1(c))return I9100_EMMC_CARD_ERROR;
  if((c->last_response&(1U<<8))&&((c->last_response>>9)&15U)==4U)
   return wait_clear(c,EM_STATUS,DATA_BUSY,500000000ULL);
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(now<start||now-start>=500000000ULL)return I9100_EMMC_TIMEOUT;
  if(c->io.delay_us(c->io.cookie,1000U))return I9100_EMMC_IO;
 }
 return I9100_EMMC_TIMEOUT;
}
static int data_read(struct i9100_emmc_card *c,unsigned cmd,uint32_t arg,uint32_t blocks,uint8_t *out)
{
 int e=wait_clear(c,EM_STATUS,DATA_BUSY,500000000ULL);if(e)return e;
 wr(c,EM_CTRL,rd(c,EM_CTRL)|(1U<<1));
 e=wait_clear(c,EM_CTRL,1U<<1,50000000ULL);if(e)return e;
 wr(c,EM_BLKSIZ,512);wr(c,EM_BYTCNT,blocks*512U);
 e=command(c,cmd,arg,C_RESP|C_CRC|C_DATA|C_WAIT);if(e)return e;
 if(r1(c))return I9100_EMMC_CARD_ERROR;
 const uint32_t words=blocks*128U;uint32_t received=0;
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(uint32_t i=0;i<c->io.poll_bound;i++){
  uint32_t irq=rd(c,EM_RINTSTS);e=interrupt_error(c,irq);if(e)return e;
  uint32_t st=rd(c,EM_STATUS),count=FIFO_WORDS(st);c->last_status=st;
  if(count>words-received)return I9100_EMMC_IO;
  for(uint32_t n=0;n<count;n++){
   uint32_t value=rd(c,EM_DATA),at=received++*4U;
   out[at]=(uint8_t)value;out[at+1]=(uint8_t)(value>>8);
   out[at+2]=(uint8_t)(value>>16);out[at+3]=(uint8_t)(value>>24);
  }
  if(irq&I_DATA_DONE){
   if(received!=words)return I9100_EMMC_IO;
   wr(c,EM_RINTSTS,I_DATA_DONE|(1U<<5));return 0;
  }
  if(received==words && FIFO_WORDS(rd(c,EM_STATUS)))return I9100_EMMC_IO;
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(now<start||now-start>=500000000ULL)return I9100_EMMC_TIMEOUT;
  if(delay(c))return I9100_EMMC_IO;
 }
 return I9100_EMMC_TIMEOUT;
}
static uint32_t csd_bits(const uint32_t r[4],unsigned low,unsigned bits)
{
 uint32_t value=0;
 for(unsigned i=0;i<bits;i++)value|=((r[3U-(low+i)/32U]>>((low+i)&31U))&1U)<<i;
 return value;
}
static uint32_t transfer_speed(const uint32_t csd[4])
{
 static const uint8_t mantissa[16]={0,10,12,13,15,20,26,30,35,40,45,52,55,60,70,80};
 static const uint32_t units[4]={100000U,1000000U,10000000U,100000000U};
 unsigned code=csd_bits(csd,96,8),unit=code&7U,mant=(code>>3)&15U;
 if(unit>3U||!mantissa[mant]||(code&128U))return 0;
 return (uint32_t)((uint64_t)units[unit]*mantissa[mant]/10U);
}
void i9100_emmc_init(struct i9100_emmc_card *c,const struct i9100_emmc_io *io)
{
 if(!c)return;
 memset(c,0,sizeof(*c));if(io)c->io=*io;
}
int i9100_emmc_probe(struct i9100_emmc_card *c)
{
 if(!c||!c->io.read32||!c->io.write32||!c->io.now_ns||!c->io.delay_us||
    !c->io.poll_bound||c->io.poll_bound>1000000U)return I9100_EMMC_ARGUMENT;
 c->ready=0;c->stage=1;
 if(c->io.input_clock_hz<400000U||c->io.input_clock_hz>200000000U)
  return c->last_error=I9100_EMMC_CLOCK;
 c->version=rd(c,EM_VERID);c->hcon=rd(c,EM_HCON);
 /* Reject the later64-bit/data@200 IP rather than guessing its layout. */
 if(!c->version||c->version==0xffffffffU||(c->version&0xffffU)<0x2000U||
    (c->version&0xffffU)>=0x240aU||((c->hcon>>7)&7U)!=1U)
  return c->last_error=I9100_EMMC_UNSUPPORTED;
 if(!(rd(c,EM_PWREN)&1U))return c->last_error=I9100_EMMC_NO_DEVICE;
 wr(c,EM_INTMASK,0);wr(c,EM_IDINTEN,0);
 wr(c,EM_BMOD,rd(c,EM_BMOD)&~(1U<<7));wr(c,EM_CTRL,RESET_BITS);
 int e=wait_clear(c,EM_CTRL,RESET_BITS,50000000ULL);if(e)goto failed;
 wr(c,EM_CTRL,rd(c,EM_CTRL)&~(CTRL_DMA|(1U<<4)));
 wr(c,EM_RINTSTS,0xffffffffU);wr(c,EM_IDSTS,0xffffffffU);
 wr(c,EM_TMOUT,0xffffffffU);wr(c,EM_CTYPE,0);wr(c,EM_UHS,0);
 wr(c,EM_FIFOTH,0); /* PIO one-word watermark; no assumed FIFO depth. */
 e=clock_set(c,400000U);if(e)goto failed;c->stage=2;
 e=command(c,0,0,C_INIT);if(e)goto failed;
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(unsigned i=0;i<1000;i++){
  e=command(c,1,0x40ff8000U,C_RESP);if(e)goto failed;
  c->ocr=c->last_response;if(c->ocr&(1U<<31))break;
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(now<start||now-start>=1000000000ULL){e=I9100_EMMC_TIMEOUT;goto failed;}
  if(c->io.delay_us(c->io.cookie,1000U)){e=I9100_EMMC_IO;goto failed;}
 }
 if(!(c->ocr&(1U<<31))){e=I9100_EMMC_TIMEOUT;goto failed;}
 if(!(c->ocr&(1U<<30))||!(c->ocr&0x00ff8000U)){e=I9100_EMMC_UNSUPPORTED;goto failed;}
 c->stage=3;e=command(c,2,0,C_RESP|C_LONG|C_CRC);if(e)goto failed;
 for(unsigned n=0;n<4;n++)c->cid[n]=rd(c,EM_RESP3-n*4U);
 if(!(c->cid[0]|c->cid[1]|c->cid[2]|c->cid[3])){e=I9100_EMMC_NO_DEVICE;goto failed;}
 c->rca=1;e=command(c,3,c->rca<<16,C_RESP|C_CRC);if(e)goto failed;
 if(r1(c)){e=I9100_EMMC_CARD_ERROR;goto failed;}
 e=command(c,9,c->rca<<16,C_RESP|C_LONG|C_CRC);if(e)goto failed;
 for(unsigned n=0;n<4;n++)c->csd[n]=rd(c,EM_RESP3-n*4U);
 if(csd_bits(c->csd,122,4)<4U){e=I9100_EMMC_UNSUPPORTED;goto failed;}
 e=command(c,7,c->rca<<16,C_RESP|C_CRC);if(e)goto failed;
 if(r1(c)){e=I9100_EMMC_CARD_ERROR;goto failed;}
 e=ready_transfer(c);if(e)goto failed;c->stage=4;
 e=data_read(c,8,0,1,c->ext_csd);if(e)goto failed;
 if(c->ext_csd[192]<2U||c->ext_csd[192]>8U||c->ext_csd[61]){e=I9100_EMMC_UNSUPPORTED;goto failed;}
 if((c->ext_csd[179]&7U)||c->ext_csd[183]||c->ext_csd[185]){e=I9100_EMMC_PARTITION;goto failed;}
 c->sectors=(uint32_t)c->ext_csd[212]|(uint32_t)c->ext_csd[213]<<8|
  (uint32_t)c->ext_csd[214]<<16|(uint32_t)c->ext_csd[215]<<24;
 if(c->sectors<4096U){e=I9100_EMMC_NO_DEVICE;goto failed;}
 c->legacy_max_hz=transfer_speed(c->csd);
 if(c->io.max_read_clock_hz&&c->legacy_max_hz){
  uint32_t target=c->io.max_read_clock_hz;
  if(target>c->legacy_max_hz)target=c->legacy_max_hz;
  if(target>20000000U)target=20000000U;
  if(target>c->clock_hz){e=clock_set(c,target);if(e)goto failed;}
 }
 c->ready=1;c->stage=5;c->last_error=0;return 0;
failed:
 c->last_error=e;c->ready=0;return e;
}
int i9100_emmc_read(struct i9100_emmc_card *c,uint64_t lba,uint32_t count,uint8_t *out)
{
 if(!c||!out||!count||count>I9100_EMMC_MAX_SECTORS)return I9100_EMMC_ARGUMENT;
 if(!c->ready)return I9100_EMMC_NO_DEVICE;
 if(lba>=c->sectors||count>c->sectors-lba||lba>UINT32_MAX||
    (uint64_t)count-1U>UINT32_MAX-lba)return I9100_EMMC_ARGUMENT;
 int e=ready_transfer(c);if(e)goto failed;
 e=data_read(c,count==1?17U:18U,(uint32_t)lba,count,out);if(e)goto failed;
 if(count>1){
  e=command(c,12,0,C_RESP|C_CRC|C_STOP);if(e)goto failed;
  if(r1(c)){e=I9100_EMMC_CARD_ERROR;goto failed;}
 }
 e=ready_transfer(c);if(e)goto failed;
 if(UINT64_MAX-c->completed_sectors<count)c->completed_sectors=UINT64_MAX;
 else c->completed_sectors+=count;
 return 0;
failed:
 c->last_error=e;c->ready=0;return e;
}
