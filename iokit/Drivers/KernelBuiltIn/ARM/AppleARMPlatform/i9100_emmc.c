/* SPDX-License-Identifier: BSD-2-Clause
 * Original read-only MMC/32-bit MSHCI bring-up, not the SD ACMD protocol.
 * Clock/power/pinmux must be established and independently verified by the
 * board owner. The existing RO path never writes a sector/configuration.
 * Explicit writer policy permits only two current-table user-area slices. */
#include "i9100_emmc.h"
#include "i9100_emmc_sdr.h"
#include "i9100_emmc_idmac.h"
#include <string.h>
#define C_START (1U<<31)
#define C_RESP (1U<<6)
#define C_LONG (1U<<7)
#define C_CRC (1U<<8)
#define C_DATA (1U<<9)
#define C_WRITE (1U<<10)
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
static int sdr_session_valid(struct i9100_emmc_card *c)
{
 struct i9100_emmc_sdr_session *s=c->sdr;
 return s&&s->self==(uintptr_t)s&&s->card==(uintptr_t)c&&s->generation==c->generation&&s->attempted==1U&&
  (s->width==4U||s->width==8U)&&s->high_speed<=1U&&s->ciu_hz==c->io.input_clock_hz&&!memcmp(s->cid,c->cid,sizeof(s->cid));
}
static int dma_binding_valid(struct i9100_emmc_card *c)
{
 struct i9100_emmc_idmac_binding *d=c->dma;
 return d&&d->self==(uintptr_t)d&&d->card==(uintptr_t)c&&d->generation==c->generation&&
  !memcmp(d->cid,c->cid,16)&&d->enabled==1U&&!d->poisoned&&d->begin&&d->finish&&d->fail;
}
int i9100_emmc_transport_check(struct i9100_emmc_card *c)
{
 if(!c||c->self!=(uintptr_t)c)return I9100_EMMC_OWNER;
 if(!c->ready)return I9100_EMMC_NO_DEVICE;
 uint32_t width=0,timing=0,ctype=0;
 if(c->sdr){
  if(!sdr_session_valid(c))return I9100_EMMC_OWNER;
  const struct i9100_emmc_sdr_session *s=c->sdr;
  if(!s->verified||s->poisoned||s->phase!=I9100_SDR_ACTIVE)return I9100_EMMC_NO_DEVICE;
  width=s->width==4U?1U:2U;timing=s->high_speed;ctype=s->width==4U?1U:1U<<16;
  if(c->clock_hz!=s->clock_hz||c->clock_div!=s->clock_div||c->ext_csd[187]!=s->power_class)
   return I9100_EMMC_CLOCK;
 }
 if((c->ext_csd[179]&7U)||c->ext_csd[183]!=width||c->ext_csd[185]!=timing||c->ext_csd[61])
  return I9100_EMMC_PARTITION;
 if(rd(c,EM_CTYPE)!=ctype||rd(c,EM_UHS)!=0U||rd(c,EM_CLKSRC)!=0U||rd(c,EM_CLKDIV)!=c->clock_div||rd(c,EM_CLKENA)!=1U)
  return I9100_EMMC_CLOCK;
 uint32_t ctrl=rd(c,EM_CTRL),bmod=rd(c,EM_BMOD);
 if(ctrl==UINT32_MAX||bmod==UINT32_MAX)return I9100_EMMC_IO;
 if(c->dma&&!dma_binding_valid(c))return I9100_EMMC_OWNER;
 if(c->dma&&c->dma->active){
  if((ctrl&CTRL_DMA)!=CTRL_DMA||!(bmod&(1U<<7)))return I9100_EMMC_OWNER;
 }else if((ctrl&CTRL_DMA)||(bmod&(1U<<7)))return I9100_EMMC_UNSUPPORTED;
 if(rd(c,EM_IDINTEN)||rd(c,EM_INTMASK))return I9100_EMMC_OWNER;
 return 0;
}
static int grant_valid(struct i9100_emmc_card *c,struct i9100_emmc_write_grant *g)
{
 if(!c||!g||c->self!=(uintptr_t)c||g->self!=(uintptr_t)g||g->card!=(uintptr_t)c||
    c->write_grant!=g||!g->enabled||g->generation!=c->generation||memcmp(g->cid,c->cid,sizeof(c->cid)))return I9100_EMMC_OWNER;
 if(g->poisoned||!c->ready)return I9100_EMMC_NO_DEVICE;
 if(c->sectors!=30777344U)return I9100_EMMC_PARTITION;
 int mode=i9100_emmc_transport_check(c);if(mode)return mode;
 uint64_t base=g->partition_role==1U?344064ULL:g->partition_role==2U?3489792ULL:0ULL;
 uint64_t span=g->partition_role==1U?3145728ULL:g->partition_role==2U?12582912ULL:0ULL;
 if(!span||g->first_lba<base||g->first_lba>=base+span||!g->sectors||g->sectors>base+span-g->first_lba)return I9100_EMMC_PARTITION;
 return 0;
}
static int write_cache_supported(struct i9100_emmc_card *c)
{
 if(!c->ext_csd[33])return 1;
 uint32_t size=(uint32_t)c->ext_csd[249]|(uint32_t)c->ext_csd[250]<<8|
  (uint32_t)c->ext_csd[251]<<16|(uint32_t)c->ext_csd[252]<<24;
 return c->ext_csd[33]==1U&&c->ext_csd[192]>=6U&&size;
}
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
static int clock_program(struct i9100_emmc_card *c,uint32_t div)
{
 if(!div||div>255U||!c->io.input_clock_hz)return I9100_EMMC_CLOCK;
 wr(c,EM_CLKENA,0);int e=clock_update(c);if(e)return e;
 if(rd(c,EM_CLKENA)!=0U)return I9100_EMMC_READBACK;
 wr(c,EM_CLKSRC,0);wr(c,EM_CLKDIV,(uint32_t)div);
 e=clock_update(c);if(e)return e;
 if(rd(c,EM_CLKSRC)!=0U||rd(c,EM_CLKDIV)!=(uint32_t)div)return I9100_EMMC_READBACK;
 wr(c,EM_CLKENA,1);e=clock_update(c);if(e)return e;
 if(rd(c,EM_CLKENA)!=1U)return I9100_EMMC_READBACK;
 c->clock_div=(uint32_t)div;c->clock_hz=c->io.input_clock_hz/(2U*c->clock_div);return 0;
}
static int clock_set(struct i9100_emmc_card *c,uint32_t target)
{
 if(!target||!c->io.input_clock_hz)return I9100_EMMC_CLOCK;
 uint64_t div=((uint64_t)c->io.input_clock_hz+2ULL*target-1)/(2ULL*target);
 if(!div)div=1;
 if(div>255U||c->io.input_clock_hz/(2U*(uint32_t)div)>target)return I9100_EMMC_CLOCK;
 return clock_program(c,(uint32_t)div);
}
static int command(struct i9100_emmc_card *c,unsigned cmd,uint32_t arg,uint32_t flags)
{
 if(c->dma&&c->dma->active){
  const struct i9100_emmc_idmac_binding *d=c->dma;
  if(!dma_binding_valid(c)||d->command!=cmd||d->argument!=arg||rd(c,EM_BYTCNT)!=d->sectors*512U||
     flags!=(C_RESP|C_CRC|C_DATA|C_WAIT|(d->writing?C_WRITE:0)))return I9100_EMMC_OWNER;
 }
 /* No general/raw-command export. Writer commands have a separately
  * current-bound capability; erase/trim/boot/RPMB remain impossible. */
 if(cmd==24U||cmd==25U){
  struct i9100_emmc_write_grant *g=c->write_grant;int e=grant_valid(c,g);if(e)return e;
  uint32_t bytes=rd(c,EM_BYTCNT);uint64_t count=bytes/512U;
  if(!(flags&C_DATA)||!(flags&C_WRITE)||!bytes||(bytes&511U)||count>I9100_EMMC_MAX_SECTORS||
     (cmd==24U&&count!=1U)||(uint64_t)arg<g->first_lba||(uint64_t)arg>=g->first_lba+g->sectors||
     count>g->first_lba+g->sectors-arg)return I9100_EMMC_PARTITION;
 }else if(cmd==6U){
  if(c->sdr&&c->sdr->switch_live){
   struct i9100_emmc_sdr_session *s=c->sdr;
   if(!sdr_session_valid(c)||s->verified||s->poisoned||c->write_grant||c->ready||arg!=s->expected_switch||
      flags!=(C_RESP|C_CRC|C_WAIT)||(arg&0xff0000ffU)!=0x03000001U)return I9100_EMMC_OWNER;
   unsigned index=(arg>>16)&255U,value=(arg>>8)&255U;
   if(!((s->phase==I9100_SDR_TIMING&&index==185U&&value==1U&&s->high_speed==1U)||
        (s->phase==I9100_SDR_CLASS&&index==187U&&value==s->power_class&&value<=15U)||
        (s->phase==I9100_SDR_WIDTH&&index==183U&&value==(s->width==4U?1U:2U))||
        (s->phase==I9100_SDR_RESTORE_WIDTH&&index==183U&&value==0U)||
        (s->phase==I9100_SDR_RESTORE_TIMING&&index==185U&&value==0U)||
        (s->phase==I9100_SDR_RESTORE_CLASS&&index==187U&&value==s->original_class&&value<=15U)))return I9100_EMMC_UNSUPPORTED;
  }else{
   int e=grant_valid(c,c->write_grant);if(e)return e;
   if(arg!=0x03200100U||flags!=(C_RESP|C_CRC|C_WAIT)||c->ext_csd[192]<6U||c->ext_csd[33]!=1U)
    return I9100_EMMC_UNSUPPORTED;
  }
 }else
 if(cmd!=0&&cmd!=1&&cmd!=2&&cmd!=3&&cmd!=7&&cmd!=8&&cmd!=9&&cmd!=12&&cmd!=13&&cmd!=17&&cmd!=18)
  return I9100_EMMC_ARGUMENT;
 int e=wait_clear(c,EM_CMD,C_START,100000000ULL);if(e)return e;
 c->last_command=cmd;wr(c,EM_RINTSTS,0xffffffffU);wr(c,EM_CMDARG,arg);
 if(rd(c,EM_CMDARG)!=arg)return I9100_EMMC_IO;
 if(c->dma&&c->dma->active)c->dma->command_submitted=1;
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
 if(c->dma&&(cmd==17U||cmd==18U)){
  if(!dma_binding_valid(c))return I9100_EMMC_OWNER;struct i9100_emmc_idmac_binding *d=c->dma;
  int e=d->begin(d->cookie,c,cmd,arg,blocks,0,0);if(e)return e;
  e=command(c,cmd,arg,C_RESP|C_CRC|C_DATA|C_WAIT);if(!e)e=r1(c);
  if(e)return d->fail(d->cookie,c,e);return d->finish(d->cookie,c,out);
 }
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
  uint32_t previous=received;
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
  /* Drain confirmed data continuously. Delaying after a successful FIFO
   * drain only throttles PIO; empty polls still use the bounded backoff. */
  if(received==previous&&delay(c))return I9100_EMMC_IO;
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
 c->self=(uintptr_t)c;
}
int i9100_emmc_probe(struct i9100_emmc_card *c)
{
 if(!c||!c->io.read32||!c->io.write32||!c->io.now_ns||!c->io.delay_us||
    !c->io.poll_bound||c->io.poll_bound>1000000U)return I9100_EMMC_ARGUMENT;
 if(c->self!=(uintptr_t)c)return I9100_EMMC_OWNER;
 if(c->write_grant||c->sdr||c->dma)return I9100_EMMC_BUSY;
 if(c->generation==UINT32_MAX)return I9100_EMMC_OWNER;
 c->generation++;
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
static int sdr_failed(struct i9100_emmc_card *c,struct i9100_emmc_sdr_session *s,int error)
{
 s->error=error;s->poisoned=1;s->verified=s->switch_live=0;s->expected_switch=0;
 s->failure_phase=s->phase;s->phase=I9100_SDR_FAILED;
 c->ready=0;c->last_error=error;return error;
}
static int sdr_ready(struct i9100_emmc_card *c,uint64_t limit)
{
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(unsigned i=0;i<c->io.poll_bound;i++){
  int e=command(c,13,c->rca<<16,C_RESP|C_CRC);if(e)return e;if(r1(c))return I9100_EMMC_CARD_ERROR;
  uint32_t status=rd(c,EM_STATUS);
  if(status==UINT32_MAX)return I9100_EMMC_IO;
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(now<start||now-start>=limit)return I9100_EMMC_TIMEOUT;
  if((c->last_response&(1U<<8))&&((c->last_response>>9)&15U)==4U&&!(status&DATA_BUSY))return 0;
  if(c->io.delay_us(c->io.cookie,1000U))return I9100_EMMC_IO;
 }
 return I9100_EMMC_TIMEOUT;
}
static int sdr_switch(struct i9100_emmc_card *c,unsigned index,unsigned value)
{
 struct i9100_emmc_sdr_session *s=c->sdr;
 s->configuration_possible=1;s->expected_switch=0x03000001U|(index<<16)|(value<<8);s->switch_live=1;
 int e=command(c,6,s->expected_switch,C_RESP|C_CRC|C_WAIT);s->switch_live=0;s->expected_switch=0;
 if(e)return e;s->switches++;if(r1(c))return I9100_EMMC_CARD_ERROR;
 return sdr_ready(c,s->switch_timeout_ns);
}
static int ext_csd_same(const uint8_t *before,const uint8_t *after)
{
 /* Every listed read-only field must agree. In particular do not copy
  * vendor's malformed negation/AND expression in mmc_compare_ext_csds. */
 static const uint16_t readonly[]={160,181,192,194,196,198,199,200,201,202,203,212,213,214,215,
  217,221,222,223,224,226,229,230,231,232,236,237,238,239,248,249,250,251,252,503};
 for(unsigned n=0;n<sizeof(readonly)/sizeof(readonly[0]);n++)if(before[readonly[n]]!=after[readonly[n]])return 0;
 /* Neither transport admission nor data IO can silently select a boot
  * area, change sector size, caching, RPMB/reset or partition settings. */
 static const uint16_t protected_control[]={33,34,61,62,156,155,161,162,166,167,175,178,179,191};
 for(unsigned n=0;n<sizeof(protected_control)/sizeof(protected_control[0]);n++)
  if(before[protected_control[n]]!=after[protected_control[n]])return 0;
 return 1;
}
static int sdr_readback(struct i9100_emmc_card *c,uint8_t out[512])
{
 int e=data_read(c,8,0,1,out);if(e)return e;e=sdr_ready(c,c->sdr->switch_timeout_ns);if(e)return e;
 struct i9100_emmc_sdr_session *s=c->sdr;
 if(!ext_csd_same(c->ext_csd,out)||out[183]!=(s->width==4U?1U:2U)||out[185]!=s->high_speed||out[187]!=s->power_class)
  return I9100_EMMC_READBACK;
 return 0;
}
int i9100_emmc_sdr_apply(int enabled,struct i9100_emmc_card *c,struct i9100_emmc_sdr_session *s,
 const struct i9100_emmc_sdr_plan *p,const struct i9100_emmc_sdr_board_ops *board)
{
 if(!enabled)return I9100_EMMC_UNSUPPORTED;
 if(enabled!=1||!c||!s||!p||!board||!board->check||!board->prepare_pads||s->self||s->attempted||
    p->board_reviewed!=1U||(p->width!=4U&&p->width!=8U)||p->high_speed>1U||p->vcc_mv!=3300U||
    !p->target_hz||p->target_hz>52000000U||p->ciu_hz!=c->io.input_clock_hz||memcmp(p->cid,c->cid,sizeof(p->cid)))
  return I9100_EMMC_ARGUMENT;
 if(c->sdr||c->write_grant||c->dma)return I9100_EMMC_BUSY;
 int e=i9100_emmc_transport_check(c);if(e)return e;
 if(c->sectors!=30777344ULL||c->ext_csd[192]<2U||c->ext_csd[192]>8U||c->ext_csd[187]>15U)return I9100_EMMC_UNSUPPORTED;
 uint32_t maximum=c->legacy_max_hz;
 if(p->high_speed)maximum=c->ext_csd[196]&2U?52000000U:c->ext_csd[196]&1U?26000000U:0U;
 if(!maximum||(!p->high_speed&&p->target_hz>20000000U)||p->target_hz>maximum||p->target_hz>p->ciu_hz/2U)
  return I9100_EMMC_CLOCK;
 e=board->check(board->cookie);if(e)return e;
 uint32_t status=rd(c,EM_STATUS),cmd=rd(c,EM_CMD);
 if(status==UINT32_MAX||cmd==UINT32_MAX||cmd&C_START||
    status&((15U<<4)|DATA_BUSY|(1U<<10)|(0x1fffU<<17)|(1U<<30)|(1U<<31)))return I9100_EMMC_BUSY;
 memset(s,0,sizeof(*s));s->self=(uintptr_t)s;s->card=(uintptr_t)c;s->generation=c->generation;s->attempted=1;
 memcpy(s->cid,c->cid,sizeof(s->cid));
 s->original_hz=c->clock_hz;s->original_div=c->clock_div;s->original_class=c->ext_csd[187];
 s->width=p->width;s->high_speed=p->high_speed;s->target_hz=p->target_hz;s->ciu_hz=p->ciu_hz;
 s->power_class=(c->ext_csd[p->target_hz<=26000000U?203U:202U]>>(p->width==8U?4U:0U))&15U;
 s->switch_timeout_ns=c->ext_csd[192]>=6U&&c->ext_csd[248]?(uint64_t)c->ext_csd[248]*10000000ULL:500000000ULL;
 c->sdr=s;c->ready=0;s->configuration_possible=1;s->phase=I9100_SDR_CLOCK_LOW;
 e=clock_set(c,400000U);if(e)return sdr_failed(c,s,e);
 e=sdr_ready(c,s->switch_timeout_ns);if(e)return sdr_failed(c,s,e);
 e=board->check(board->cookie);if(e)return sdr_failed(c,s,e);
 s->phase=I9100_SDR_PADS;e=board->prepare_pads(board->cookie,p->width);if(e)return sdr_failed(c,s,e);
 e=board->check(board->cookie);if(e)return sdr_failed(c,s,e);
 if(p->high_speed){s->phase=I9100_SDR_TIMING;e=sdr_switch(c,185,1);if(e)return sdr_failed(c,s,e);}
 if(c->ext_csd[187]!=s->power_class){s->phase=I9100_SDR_CLASS;e=sdr_switch(c,187,s->power_class);if(e)return sdr_failed(c,s,e);}
 s->phase=I9100_SDR_WIDTH;e=sdr_switch(c,183,p->width==4U?1U:2U);if(e)return sdr_failed(c,s,e);
 uint32_t ctype=p->width==4U?1U:1U<<16;wr(c,EM_CTYPE,ctype);
 if(rd(c,EM_CTYPE)!=ctype||rd(c,EM_UHS)!=0U)return sdr_failed(c,s,I9100_EMMC_READBACK);
 uint8_t observed[512];s->phase=I9100_SDR_VERIFY_SLOW;e=sdr_readback(c,observed);if(e)return sdr_failed(c,s,e);
 e=board->check(board->cookie);if(e)return sdr_failed(c,s,e);
 s->phase=I9100_SDR_CLOCK_FAST;e=clock_set(c,p->target_hz);if(e)return sdr_failed(c,s,e);
 e=sdr_readback(c,observed);if(e)return sdr_failed(c,s,e);
 e=board->check(board->cookie);if(e)return sdr_failed(c,s,e);
 memcpy(c->ext_csd,observed,sizeof(observed));s->clock_hz=c->clock_hz;s->clock_div=c->clock_div;
 s->phase=I9100_SDR_ACTIVE;s->verified=1;c->ready=1;c->last_error=0;
 e=i9100_emmc_transport_check(c);return e?sdr_failed(c,s,e):0;
}
int i9100_emmc_sdr_restore(struct i9100_emmc_card *c,struct i9100_emmc_sdr_session *s)
{
 if(!c||!s||c->sdr!=s||!sdr_session_valid(c))return I9100_EMMC_OWNER;
 if(c->write_grant||c->dma)return I9100_EMMC_BUSY;
 if(s->poisoned||s->error)return s->error?s->error:I9100_EMMC_NO_DEVICE;
 int e=i9100_emmc_transport_check(c);if(e)return sdr_failed(c,s,e);
 uint32_t status=rd(c,EM_STATUS),cmd=rd(c,EM_CMD);
 if(status==UINT32_MAX||cmd==UINT32_MAX)return I9100_EMMC_IO;
 if(cmd&C_START||status&((15U<<4)|DATA_BUSY|(1U<<10)|(0x1fffU<<17)|(1U<<30)|(1U<<31)))return I9100_EMMC_BUSY;
 c->ready=0;s->verified=0;
 e=clock_set(c,400000U);if(e)return sdr_failed(c,s,e);
 s->phase=I9100_SDR_RESTORE_WIDTH;e=sdr_switch(c,183,0);if(e)return sdr_failed(c,s,e);
 wr(c,EM_CTYPE,0);if(rd(c,EM_CTYPE)!=0U)return sdr_failed(c,s,I9100_EMMC_READBACK);
 if(s->high_speed){s->phase=I9100_SDR_RESTORE_TIMING;e=sdr_switch(c,185,0);if(e)return sdr_failed(c,s,e);}
 if(s->power_class!=s->original_class){s->phase=I9100_SDR_RESTORE_CLASS;e=sdr_switch(c,187,s->original_class);if(e)return sdr_failed(c,s,e);}
 uint8_t observed[512];e=data_read(c,8,0,1,observed);if(!e)e=sdr_ready(c,s->switch_timeout_ns);
 if(!e&&(!ext_csd_same(c->ext_csd,observed)||observed[183]||observed[185]||observed[187]!=s->original_class))e=I9100_EMMC_READBACK;
 if(e)return sdr_failed(c,s,e);
 e=clock_program(c,s->original_div);if(e)return sdr_failed(c,s,e);
 e=data_read(c,8,0,1,observed);if(!e)e=sdr_ready(c,s->switch_timeout_ns);
 if(!e&&(!ext_csd_same(c->ext_csd,observed)||observed[183]||observed[185]||observed[187]!=s->original_class))e=I9100_EMMC_READBACK;
 if(e)return sdr_failed(c,s,e);
 memcpy(c->ext_csd,observed,sizeof(observed));s->phase=I9100_SDR_RESTORED;c->sdr=0;c->ready=1;c->last_error=0;
 e=i9100_emmc_transport_check(c);
 if(e){c->sdr=s;return sdr_failed(c,s,e);}return 0;
}
int i9100_emmc_read(struct i9100_emmc_card *c,uint64_t lba,uint32_t count,uint8_t *out)
{
 if(!c||!out||!count||count>I9100_EMMC_MAX_SECTORS)return I9100_EMMC_ARGUMENT;
 if(c->self!=(uintptr_t)c)return I9100_EMMC_OWNER;
 if(!c->ready)return I9100_EMMC_NO_DEVICE;
 if(lba>=c->sectors||count>c->sectors-lba||lba>UINT32_MAX||
    (uint64_t)count-1U>UINT32_MAX-lba)return I9100_EMMC_ARGUMENT;
 int e=i9100_emmc_transport_check(c);if(e)goto failed;
 e=ready_transfer(c);if(e)goto failed;
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
static int writer_failed(struct i9100_emmc_card *c,struct i9100_emmc_write_grant *g,int e)
{
 if(!g->first_error)g->first_error=e;g->poisoned=1;c->last_error=e;c->ready=0;return e;
}
static int data_write(struct i9100_emmc_card *c,struct i9100_emmc_write_grant *g,uint64_t lba,uint32_t blocks,const uint8_t *bytes)
{
 if(c->dma){
  if(!dma_binding_valid(c))return I9100_EMMC_OWNER;struct i9100_emmc_idmac_binding *d=c->dma;
  unsigned cmd=blocks==1U?24U:25U;int e=d->begin(d->cookie,c,cmd,(uint32_t)lba,blocks,1,bytes);if(e)return e;
  g->words_submitted=g->ciu_bytes=g->host_bytes=0;g->dirty=g->mutation_possible=1;
  e=command(c,cmd,(uint32_t)lba,C_RESP|C_CRC|C_DATA|C_WRITE|C_WAIT);if(!e)e=r1(c);
  if(e)return d->fail(d->cookie,c,e);e=d->finish(d->cookie,c,0);
  if(!e){g->ciu_bytes=d->ciu_bytes;g->host_bytes=d->host_bytes;}return e;
 }
 uint32_t ctrl=rd(c,EM_CTRL),bmod=rd(c,EM_BMOD);
 if(ctrl==UINT32_MAX||bmod==UINT32_MAX||(ctrl&CTRL_DMA)||(bmod&(1U<<7))||rd(c,EM_IDINTEN))return I9100_EMMC_UNSUPPORTED;
 int e=wait_clear(c,EM_STATUS,DATA_BUSY,500000000ULL);if(e)return e;
 wr(c,EM_CTRL,rd(c,EM_CTRL)|(1U<<1));e=wait_clear(c,EM_CTRL,1U<<1,50000000ULL);if(e)return e;
 uint32_t initial_fifo=rd(c,EM_STATUS);
 if(initial_fifo==UINT32_MAX||FIFO_WORDS(initial_fifo)||(initial_fifo&(1U<<3)))return I9100_EMMC_IO;
 wr(c,EM_BLKSIZ,512U);wr(c,EM_BYTCNT,blocks*512U);
 if(rd(c,EM_BLKSIZ)!=512U||rd(c,EM_BYTCNT)!=blocks*512U)return I9100_EMMC_IO;
 g->words_submitted=g->ciu_bytes=g->host_bytes=0;g->dirty=g->mutation_possible=1;
 e=command(c,blocks==1U?24U:25U,(uint32_t)lba,C_RESP|C_CRC|C_DATA|C_WRITE|C_WAIT);
 if(e)return e;if(r1(c))return I9100_EMMC_CARD_ERROR;
 uint32_t words=blocks*128U;uint64_t start=c->io.now_ns(c->io.cookie);
 for(uint32_t n=0;n<c->io.poll_bound;n++){
  uint32_t irq=rd(c,EM_RINTSTS);e=interrupt_error(c,irq);if(e)return e;
  uint32_t status=rd(c,EM_STATUS);c->last_status=status;
  uint32_t previous=g->words_submitted;
  if(status==UINT32_MAX)return I9100_EMMC_IO;
  if(irq&I_DATA_DONE){
   g->ciu_bytes=rd(c,EM_TCBCNT);g->host_bytes=rd(c,EM_TBBCNT);
   if(g->words_submitted!=words||g->ciu_bytes!=blocks*512U||g->host_bytes!=blocks*512U||FIFO_WORDS(status))return I9100_EMMC_IO;
   wr(c,EM_RINTSTS,I_DATA_DONE|(1U<<4));return 0;
  }
  /* FIFO full is an actual boundary; no assumed depth or speculative burst. */
  if(g->words_submitted<words&&!(status&(1U<<3))){
   uint32_t at=g->words_submitted*4U,value=(uint32_t)bytes[at]|(uint32_t)bytes[at+1]<<8|
     (uint32_t)bytes[at+2]<<16|(uint32_t)bytes[at+3]<<24;
   wr(c,EM_DATA,value);g->words_submitted++;
  }
  uint64_t now=c->io.now_ns(c->io.cookie);if(now<start||now-start>=500000000ULL)return I9100_EMMC_TIMEOUT;
  /* Read FULL again before every word: no guessed FIFO capacity or burst.
   * Sleep only on no progress, preserving time/error/poll bounds. */
  if(g->words_submitted==previous&&delay(c))return I9100_EMMC_IO;
 }return I9100_EMMC_TIMEOUT;
}
int i9100_emmc_write(struct i9100_emmc_card *c,struct i9100_emmc_write_grant *g,uint64_t lba,uint32_t count,const uint8_t *bytes)
{
 if(!bytes||!count||count>I9100_EMMC_MAX_SECTORS)return I9100_EMMC_ARGUMENT;
 int e=grant_valid(c,g);if(e)return e;
 if(lba<g->first_lba||lba>=g->first_lba+g->sectors||count>g->first_lba+g->sectors-lba||
    lba>UINT32_MAX||(uint64_t)count-1U>UINT32_MAX-lba)return I9100_EMMC_PARTITION;
 if(!write_cache_supported(c))return I9100_EMMC_UNSUPPORTED;
 e=ready_transfer(c);if(e)return writer_failed(c,g,e);
 e=data_write(c,g,lba,count,bytes);if(e)return writer_failed(c,g,e);
 if(count>1U){e=command(c,12U,0,C_RESP|C_CRC|C_STOP);if(e)return writer_failed(c,g,e);if(r1(c))return writer_failed(c,g,I9100_EMMC_CARD_ERROR);}
 e=ready_transfer(c);if(e)return writer_failed(c,g,e);
 g->completed_sectors=UINT64_MAX-g->completed_sectors<count?UINT64_MAX:g->completed_sectors+count;
 return 0;
}
int i9100_emmc_synchronize(struct i9100_emmc_card *c,struct i9100_emmc_write_grant *g)
{
 int e=grant_valid(c,g);if(e)return e;
 if(!write_cache_supported(c))return I9100_EMMC_UNSUPPORTED;
 e=ready_transfer(c);if(e)return writer_failed(c,g,e);
 if(c->ext_csd[33]){
  e=command(c,6U,0x03200100U,C_RESP|C_CRC|C_WAIT);if(e)return writer_failed(c,g,e);
  if(r1(c))return writer_failed(c,g,I9100_EMMC_CARD_ERROR);
  e=ready_transfer(c);if(e)return writer_failed(c,g,e);
 }
 if(FIFO_WORDS(rd(c,EM_STATUS))||(rd(c,EM_STATUS)&DATA_BUSY))return writer_failed(c,g,I9100_EMMC_BUSY);
 g->dirty=0;if(g->syncs!=UINT32_MAX)g->syncs++;return 0;
}
