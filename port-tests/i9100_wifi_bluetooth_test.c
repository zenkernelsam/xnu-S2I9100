/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_sdio.h"
#include "i9100_bcm4330.h"
#include "i9100_bluetooth_h4.h"
struct model {
 uint8_t regs[256],functions[3][131072];
 unsigned locked,commands,clock_stall,reset_stall,command_stall,crc,r5_error,no_ready,freeze;
 unsigned fn,address,increment,write,unit,blocks,cursor,block_progress,ready;
 uint64_t ns;
 uint8_t window[3],clock,bp_data[4],frame[2048],hci[259];
 unsigned transactions,fail_transaction,tx_bytes,tx_fail,partial,quiesce_fail;
};
static struct model m;
static uint16_t le16(const uint8_t *b){return (uint16_t)(b[0]|((uint16_t)b[1]<<8));}
static uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put16(uint8_t *b,uint16_t v){b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *b,uint32_t v){for(unsigned n=0;n<4;n++)b[n]=(uint8_t)(v>>(n*8));}
static int lock(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint64_t now(void *c){return ((struct model *)c)->ns;}
static int delay(void *c,uint32_t us){struct model *p=c;if(!p->freeze)p->ns+=us*1000ULL;return 0;}
static uint8_t r8(void *c,unsigned off){struct model *p=c;assert(p->locked&&off<256);return p->regs[off];}
static uint16_t r16(void *c,unsigned off){struct model *p=c;assert(p->locked&&off<255);if(off==44&&!p->clock_stall&&(p->regs[44]&1U))p->regs[44]|=2U;return le16(p->regs+off);}
static void advance(struct model *p,unsigned amount)
{
 p->cursor+=amount;p->block_progress+=amount;
 if(p->block_progress>=p->unit){p->block_progress=0;if(--p->blocks)put32(p->regs+48,le32(p->regs+48)|p->ready);else put32(p->regs+48,le32(p->regs+48)|2U);}
}
static uint32_t r32(void *c,unsigned off)
{
 struct model *p=c;assert(p->locked&&off<253);
 if(off==32){assert(!p->write&&p->blocks);unsigned amount=p->unit-p->block_progress<4U?p->unit-p->block_progress:4U;
  uint32_t value=0;for(unsigned n=0;n<amount;n++)value|=(uint32_t)p->functions[p->fn][p->address+(p->increment?p->cursor+n:0)]<<(n*8);
  advance(p,amount);return value;}
 return le32(p->regs+off);
}
static void w8(void *c,unsigned off,uint8_t value){struct model *p=c;assert(p->locked&&off<256);p->regs[off]=value;if(off==47&&!p->reset_stall)p->regs[off]=0;}
static void issue(struct model *p,uint16_t command)
{
 unsigned index=command>>8;uint32_t arg=le32(p->regs+8),response=0;p->commands++;
 if(p->command_stall)return;
 switch(index){
 case 0:assert(!(command&255));break;
 case 5:assert((command&255)==2U);response=0xa0ff8000U;break;
 case 3:response=0x12340000U;break;
 case 7:assert(arg==0x12340000U);break;
 case 52:{unsigned fn=(arg>>28)&7U,address=(arg>>9)&0x1ffffU;assert(fn<=2);
   if(arg&0x80000000U){p->functions[fn][address]=(uint8_t)arg;if(!fn&&address==2&&!p->no_ready)p->functions[0][3]=(uint8_t)arg;}
   response=p->functions[fn][address]|p->r5_error;break;}
 case 53:{
   p->fn=(arg>>28)&7U;p->address=(arg>>9)&0x1ffffU;p->increment=!!(arg&(1U<<26));p->write=!!(arg&(1U<<31));
   p->unit=le16(p->regs+4);p->blocks=le16(p->regs+6);p->cursor=p->block_progress=0;
   unsigned count=arg&511U;if(!(arg&(1U<<27)))assert((count?count:512U)==p->unit);else assert(count==p->blocks);
   p->ready=p->write?16U:32U;response=p->r5_error;put32(p->regs+48,p->ready);break;}
 default:assert(!"Memory-SD or unknown command on SDIO path");
 }
 put32(p->regs+16,response);put32(p->regs+48,le32(p->regs+48)|1U|(p->crc?(1U<<21)|0x8000U:0U));
}
static void w16(void *c,unsigned off,uint16_t value){struct model *p=c;assert(p->locked&&off<255);put16(p->regs+off,value);if(off==14)issue(p,value);}
static void w32(void *c,unsigned off,uint32_t value)
{
 struct model *p=c;assert(p->locked&&off<253);
 if(off==48){put32(p->regs+48,le32(p->regs+48)&~value);return;}
 if(off==32){assert(p->write&&p->blocks);unsigned amount=p->unit-p->block_progress<4U?p->unit-p->block_progress:4U;
  for(unsigned n=0;n<amount;n++)p->functions[p->fn][p->address+(p->increment?p->cursor+n:0)]=(uint8_t)(value>>(n*8));advance(p,amount);return;}
 put32(p->regs+off,value);
}
static struct i9100_sdio_io sd_io={&m,r8,r16,r32,w8,w16,w32,lock,unlock,now,delay};
static void card(struct i9100_sdio *s)
{
 memset(&m,0,sizeof(m));memset(s,0,sizeof(*s));put16(m.regs+254,1);put32(m.regs+64,1U<<24);
 m.functions[0][0]=0x22;m.functions[0][8]=2;m.functions[0][0x109]=0;m.functions[0][0x10a]=0x10;m.functions[0][0x10b]=0;
 const uint8_t cis[]={0,0x21,2,0xa0,0xb0,0x20,4,0xd0,2,0x30,0x43,0xff};memcpy(m.functions[0]+0x1000,cis,sizeof(cis));
 assert(!i9100_sdio_init(s,&sd_io,48000000));assert(!m.commands);
}
static void sdio(void)
{
 struct i9100_sdio s;card(&s);assert(i9100_sdio_enable(&s,1,0)==I9100_SDIO_ARGUMENT&&!m.commands);
 assert(!i9100_sdio_enable(&s,1,1));assert(s.vendor==0x02d0&&s.device==0x4330&&s.clock_hz<=400000&&s.rca==0x1234);
 assert(!i9100_sdio_function(&s,1,1)&&s.block_size[1]==64);assert(!i9100_sdio_function(&s,2,1)&&s.block_size[2]==512);
 assert(!i9100_sdio_set_bus(&s,1,25000000)&&s.clock_hz==24000000&&(m.functions[0][7]&3U)==2U&&(m.regs[40]&2U));
 uint8_t data[1024],out[1024];for(unsigned n=0;n<1024;n++)data[n]=(uint8_t)(n*13U);
 assert(!i9100_sdio_cmd53(&s,1,1,0x200,1,data,1024));memset(out,0,sizeof(out));
 assert(!i9100_sdio_cmd53(&s,0,1,0x200,1,out,1024)&&!memcmp(data,out,1024));
 assert(!i9100_sdio_cmd53(&s,1,2,0x80,1,data,512));assert(!i9100_sdio_cmd53(&s,0,2,0x80,1,out,3)&&!memcmp(data,out,3));
 assert(i9100_sdio_cmd53(&s,0,1,0x1ffff,1,out,2)==I9100_SDIO_ARGUMENT);
 unsigned before=m.commands;m.crc=1;assert(i9100_sdio_cmd53(&s,0,1,0x200,1,out,64)==I9100_SDIO_CRC&&s.fault&&m.commands==before+1);
 m.crc=0;assert(!i9100_sdio_enable(&s,0,1)&&!s.active&&!m.regs[41]);
 card(&s);m.r5_error=0x100;assert(i9100_sdio_enable(&s,1,1)==I9100_SDIO_CARD&&s.fault&&!s.active);
 card(&s);m.clock_stall=m.freeze=1;s.poll_limit=9;assert(i9100_sdio_enable(&s,1,1)==I9100_SDIO_TIMEOUT);
 card(&s);m.functions[0][0x109]=255;m.functions[0][0x10a]=255;m.functions[0][0x10b]=1;m.functions[0][0x1ffff]=0x20;
 assert(i9100_sdio_enable(&s,1,1)==I9100_SDIO_CARD);
 card(&s);assert(!i9100_sdio_enable(&s,1,1));m.no_ready=1;assert(i9100_sdio_function(&s,2,1)==I9100_SDIO_TIMEOUT&&s.fault);
}
static int transact(struct model *p){return ++p->transactions==p->fail_transaction?-1:0;}
static int cmd52(void *c,int write,unsigned fn,uint32_t addr,uint8_t *v)
{
 struct model *p=c;assert(p->locked&&fn==1);
 if(addr>=0x1000a&&addr<=0x1000c){if(write)p->window[addr-0x1000a]=*v;else *v=p->window[addr-0x1000a];}
 else{assert(addr==0x1000e);if(write)p->clock=*v;else *v=p->clock|0x40U;}
 return transact(p);
}
static int cmd53(void *c,int write,unsigned fn,uint32_t addr,int increment,uint8_t *out,uint32_t length)
{
 struct model *p=c;assert(p->locked&&!write&&fn==1&&increment&&length==4);
 uint32_t address=((uint32_t)p->window[2]<<24)|((uint32_t)p->window[1]<<16)|((uint32_t)(p->window[0]&128U)<<8)|(addr&0x7fffU);
 assert(addr&0x8000U);assert(address>=0x18000000&&address<0x18100000);memcpy(out,p->bp_data,4);return transact(p);
}
static int send_frame(void *c,const uint8_t *frame,uint32_t bytes){struct model *p=c;assert(p->locked&&bytes<=2048);memcpy(p->frame,frame,bytes);p->tx_bytes=bytes;return p->tx_fail?-1:0;}
static struct i9100_bcm_io bcm_io={&m,lock,unlock,cmd52,cmd53,send_frame,now,delay};
static void bcm_init(struct i9100_bcm4330 *b)
{
 memset(&m,0,sizeof(m));memset(b,0,sizeof(*b));m.window[0]=128;m.window[1]=3;m.window[2]=2;put32(m.bp_data,0x00104330);
 assert(!i9100_bcm_init(b,&bcm_io));assert(!m.transactions);assert(!i9100_bcm_enable(b,1,0x02d0,0x4330));
 assert(m.window[0]==128&&m.window[1]==3&&m.window[2]==2);assert(!i9100_bcm_protocol_start(b,4,4));
}
static void hw_header(uint8_t *frame,unsigned length,unsigned seq,unsigned channel,unsigned max)
{memset(frame,0,length);put16(frame,(uint16_t)length);put16(frame+2,(uint16_t)~length);frame[4]=(uint8_t)seq;frame[5]=(uint8_t)channel;frame[7]=12;frame[9]=(uint8_t)max;}
static void bcm(void)
{
 struct i9100_bcm4330 b;bcm_init(&b);uint8_t ethernet[64]={0};assert(!i9100_bcm_send_data(&b,ethernet,sizeof(ethernet),3));
 assert(le16(m.frame)==80&&le16(m.frame+2)==(uint16_t)~80U&&m.frame[4]==0&&m.frame[12]==0x20&&m.frame[13]==3&&b.tx_sequence==1);
 uint8_t reply[96];hw_header(reply,80,255,2,5);reply[12]=0x20;reply[13]=3;memcpy(reply+16,ethernet,64);
 struct i9100_bcm_frame out;assert(!i9100_bcm_receive(&b,reply,80,&out)&&out.channel==2&&out.bytes==64&&b.rx_sequence==0);
 hw_header(reply,80,0,2,5);reply[12]=0x20;assert(!i9100_bcm_receive(&b,reply,80,&out));
 assert(!i9100_bcm_ioctl(&b,262,1,(const uint8_t *)"txglom\0",7));uint16_t id=b.request_id;
 hw_header(reply,28,1,0,8);put32(reply+12,262);put32(reply+16,0);put32(reply+20,(uint32_t)id<<16);put32(reply+24,0);
 assert(!i9100_bcm_receive(&b,reply,28,&out)&&!b.pending&&!i9100_bcm_poll_request(&b));
 b.tx_max=b.tx_sequence+1U;assert(i9100_bcm_send_data(&b,ethernet,64,0)==I9100_BCM_CREDIT);
 b.tx_max=b.tx_sequence+4U;assert(!i9100_bcm_ioctl(&b,262,0,0,0));m.ns+=500000001ULL;
 assert(i9100_bcm_poll_request(&b)==I9100_BCM_TIMEOUT&&b.fault);
 bcm_init(&b);hw_header(reply,80,0,2,5);reply[12]=0x20;reply[3]^=1;
 assert(i9100_bcm_receive(&b,reply,80,&out)==I9100_BCM_FRAME&&b.fault);
 bcm_init(&b);uint32_t value=0xdeadbeef;m.fail_transaction=m.transactions+5;
 assert(i9100_bcm_backplane_read(&b,0x18001000,&value)==I9100_BCM_IO&&value==0xdeadbeef&&b.fault);
 bcm_init(&b);m.tx_fail=1;assert(i9100_bcm_send_data(&b,ethernet,64,0)==I9100_BCM_IO&&!b.tx_sequence&&b.fault);
}
static int hci_write(void *c,const uint8_t *bytes,unsigned length,unsigned *sent)
{struct model *p=c;assert(p->locked&&length<=259);memcpy(p->hci,bytes,length);*sent=p->partial?length-1:length;return p->tx_fail?-1:0;}
static int quiesce(void *c){return ((struct model *)c)->quiesce_fail?-1:0;}
static struct i9100_bt_io bt_io={&m,lock,unlock,hci_write,quiesce,now};
static void bt_init(struct i9100_bt_h4 *b)
{memset(&m,0,sizeof(m));memset(b,0,sizeof(*b));assert(!i9100_bt_init(b,&bt_io));assert(!i9100_bt_enable(b,1,1));}
static void bluetooth(void)
{
 const uint8_t complete[]={4,0x0e,4,1,3,0x0c,0};
 for(unsigned split=1;split<sizeof(complete);split++){
  struct i9100_bt_h4 b;bt_init(&b);assert(!i9100_bt_command(&b,0x0c03,0,0));assert(!memcmp(m.hci,"\1\3\14\0",4));
  unsigned consumed;struct i9100_bt_frame out;
  assert(!i9100_bt_feed(&b,complete,split,&consumed,&out)&&consumed==split);
  assert(i9100_bt_feed(&b,complete+split,sizeof(complete)-split,&consumed,&out)==1&&!b.pending&&!b.last_status&&b.last_completion==0x0e);
 }
 struct i9100_bt_h4 b;bt_init(&b);assert(!i9100_bt_command(&b,0x0405,0,0));
 uint8_t status[]={4,0x0f,4,0,1,5,4};unsigned consumed;struct i9100_bt_frame out;
 assert(i9100_bt_feed(&b,status,sizeof(status),&consumed,&out)==1&&!b.pending&&b.last_completion==0x0f);
 uint8_t bad[]={2,0,0,1,0x10};assert(i9100_bt_feed(&b,bad,sizeof(bad),&consumed,&out)==I9100_BT_FRAME&&b.fault);
 assert(!i9100_bt_enable(&b,0,0));assert(i9100_bt_enable(&b,1,1)==I9100_BT_BUSY);assert(!i9100_bt_enable(&b,1,2));
 m.partial=1;assert(i9100_bt_command(&b,0x0c03,0,0)==I9100_BT_IO&&b.fault);
 bt_init(&b);assert(!i9100_bt_command(&b,0x1001,0,0));m.ns=1000000001ULL;
 assert(i9100_bt_poll(&b)==I9100_BT_TIMEOUT&&b.fault);
 uint8_t hcd[]={0x2e,0xfc,0,0x4c,0xfc,5,0,0,0x20,0,0xab,0x4e,0xfc,4,0,0,0x20,0};uint32_t count=99;
 assert(!i9100_bt_hcd_validate(hcd,sizeof(hcd),0x00200000,0x10000,&count)&&count==3);
 hcd[8]=0x30;assert(i9100_bt_hcd_validate(hcd,sizeof(hcd),0x00200000,0x10000,&count)==I9100_BT_FRAME&&!count);
}
int main(void)
{sdio();bcm();bluetooth();puts("PASS SDIO enumeration/CIS/CCCR/FBR/CMD52/53/byte512/multiblock/CRC/clock stalls; BCM window/SDPCM credits/CDC reply/timeout/partial IO; H4 fragmentation/status/length/re-sync/HCD ranges");return 0;}
