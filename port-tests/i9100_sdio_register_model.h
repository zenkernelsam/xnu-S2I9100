/* BSD-2-Clause; SDHCI/card register boundary model, no driver function replacement. */
#ifndef I9100_SDIO_REGISTER_MODEL_H
#define I9100_SDIO_REGISTER_MODEL_H
#include <assert.h>
#include <stdint.h>
#include <string.h>
namespace i9100_card_model {
struct model {
 uint8_t regs[256],functions[3][131072];
 unsigned locked,commands,clock_stall,reset_stall,command_stall,crc,r5_error,no_ready,freeze;
 unsigned command_count[64],drop_feedback_restore;
 uint32_t original_feedback;
 unsigned fn,address,increment,write,unit,blocks,cursor,block_progress,ready;
 uint64_t ns;
 uint8_t window[3],clock,bp_data[4],frame[2048],hci[259];
 unsigned transactions,fail_transaction,tx_bytes,tx_fail,partial,quiesce_fail;
 unsigned register_writes,fail_register_write;
};
static struct model m;
static __attribute__((unused)) uint16_t le16(const uint8_t *b){return (uint16_t)(b[0]|((uint16_t)b[1]<<8));}
static __attribute__((unused)) uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static __attribute__((unused)) void put16(uint8_t *b,uint16_t v){b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);}
static __attribute__((unused)) void put32(uint8_t *b,uint32_t v){for(unsigned n=0;n<4;n++)b[n]=(uint8_t)(v>>(n*8));}
static __attribute__((unused)) int lock(void *c){struct model *p=(struct model *)c;if(p->locked)return -1;p->locked=1;return 0;}
static __attribute__((unused)) void unlock(void *c){struct model *p=(struct model *)c;assert(p->locked);p->locked=0;}
static __attribute__((unused)) uint64_t now(void *c){return ((struct model *)c)->ns;}
static __attribute__((unused)) int delay(void *c,uint32_t us){struct model *p=(struct model *)c;if(!p->freeze)p->ns+=us*1000ULL;return 0;}
static __attribute__((unused)) uint8_t r8(void *c,unsigned off){struct model *p=(struct model *)c;assert(p->locked&&off<256);return p->regs[off];}
static __attribute__((unused)) uint16_t r16(void *c,unsigned off){struct model *p=(struct model *)c;assert(p->locked&&off<255);if(off==44&&!p->clock_stall&&(p->regs[44]&1U))p->regs[44]|=2U;return le16(p->regs+off);}
static __attribute__((unused)) void advance(struct model *p,unsigned amount)
{
 p->cursor+=amount;p->block_progress+=amount;
 if(p->block_progress>=p->unit){p->block_progress=0;if(--p->blocks)put32(p->regs+48,le32(p->regs+48)|p->ready);else put32(p->regs+48,le32(p->regs+48)|2U);}
}
static __attribute__((unused)) uint32_t r32(void *c,unsigned off)
{
 struct model *p=(struct model *)c;assert(p->locked&&off<253);
 if(off==32){assert(!p->write&&p->blocks);unsigned amount=p->unit-p->block_progress<4U?p->unit-p->block_progress:4U;
  uint32_t value=0;for(unsigned n=0;n<amount;n++)value|=(uint32_t)p->functions[p->fn][p->address+(p->increment?p->cursor+n:0)]<<(n*8);
  advance(p,amount);return value;}
 return le32(p->regs+off);
}
static __attribute__((unused)) void w8(void *c,unsigned off,uint8_t value){struct model *p=(struct model *)c;assert(p->locked&&off<256);p->regs[off]=value;if(off==47&&!p->reset_stall)p->regs[off]=0;}
static __attribute__((unused)) void issue(struct model *p,uint16_t command)
{
 unsigned index=command>>8;uint32_t arg=le32(p->regs+8),response=0;
 assert(index<64);p->commands++;p->command_count[index]++;
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
   assert(p->fn>=1&&p->fn<=2&&p->unit&&p->blocks);
   assert(!p->increment||p->unit*p->blocks<=0x20000U-p->address);
   unsigned count=arg&511U;if(!(arg&(1U<<27)))assert((count?count:512U)==p->unit);else assert(count==p->blocks);
   p->ready=p->write?16U:32U;response=p->r5_error;put32(p->regs+48,p->ready);break;}
 default:assert(!"Memory-SD or unknown command on SDIO path");
 }
 put32(p->regs+16,response);put32(p->regs+48,le32(p->regs+48)|1U|(p->crc?(1U<<21)|0x8000U:0U));
}
static __attribute__((unused)) void w16(void *c,unsigned off,uint16_t value){struct model *p=(struct model *)c;assert(p->locked&&off<255);put16(p->regs+off,value);if(off==14)issue(p,value);}
static __attribute__((unused)) void w32(void *c,unsigned off,uint32_t value)
{
 struct model *p=(struct model *)c;assert(p->locked&&off<253);
 if(++p->register_writes==p->fail_register_write)return;
 if(off==132&&p->drop_feedback_restore&&value==p->original_feedback)return;
 if(off==48){put32(p->regs+48,le32(p->regs+48)&~value);return;}
 if(off==32){assert(p->write&&p->blocks);unsigned amount=p->unit-p->block_progress<4U?p->unit-p->block_progress:4U;
  for(unsigned n=0;n<amount;n++)p->functions[p->fn][p->address+(p->increment?p->cursor+n:0)]=(uint8_t)(value>>(n*8));advance(p,amount);return;}
 put32(p->regs+off,value);
}

static void reset()
{
 memset(&m,0,sizeof(m));put16(m.regs+254,1);put32(m.regs+64,1U<<24);
 put32(m.regs+52,0x11002200U);put32(m.regs+56,0x00400080U);
 put32(m.regs+128,0x12345a03U);m.original_feedback=0x01020304U;
 put32(m.regs+132,m.original_feedback);
 m.functions[0][0]=0x22;m.functions[0][8]=2;
 m.functions[0][7]=0xa0;
 m.functions[0][0x109]=0;m.functions[0][0x10a]=0x10;
 const uint8_t cis[]={0,0x21,2,0xa0,0xb0,0x20,4,0xd0,2,0x30,0x43,0xff};
 memcpy(m.functions[0]+0x1000,cis,sizeof(cis));
}
}
#endif
