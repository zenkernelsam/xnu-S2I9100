/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_bcm_chip.h"
/* Independent wire stand-in: real F1 window selectors/4-byte flag and DMP
 * descriptors, not a core-ops mock that bypasses address translation. Core
 * reset, RAM data and radio writes are deliberately absent from this model. */
struct model {
 uint32_t erom[I9100_BCM_EROM_WORDS],words,chip_id,eromptr,coreinfo,bankidx;
 uint32_t banks[I9100_BCM_MAX_BANKS],arm_reset,ram_reset,ram_clock;
 uint8_t window[3],clock;
 unsigned locked,ops,fail_op,ignore_window_restore,ignore_bank_restore,bank_writes;
 uint64_t ns,step;
};
static struct model m;
static struct i9100_bcm4330 bus;
static struct i9100_bcm_chip chip;
static int lock(void *cookie){struct model *p=cookie;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *cookie){struct model *p=cookie;assert(p->locked);p->locked=0;}
static uint64_t now(void *cookie){return ((struct model *)cookie)->ns;}
static int delay(void *cookie,uint32_t us){((struct model *)cookie)->ns+=(uint64_t)us*1000U;return 0;}
static int operation(struct model *p){p->ns+=p->step;return ++p->ops==p->fail_op?-1:0;}
static int cmd52(void *cookie,int write,unsigned fn,uint32_t addr,uint8_t *v)
{
 struct model *p=cookie;assert(p->locked&&fn==1U);
 if(addr>=0x1000aU&&addr<=0x1000cU){
  unsigned n=addr-0x1000aU;const uint8_t original[3]={0x80,0x70,0x16};
  if(write){if(!(p->ignore_window_restore&&*v==original[n]))p->window[n]=*v;}
  else *v=p->window[n];
 }else{assert(addr==0x1000eU);if(write)p->clock=*v;else *v=p->clock|0x40U;}
 return operation(p);
}
static uint32_t word_read(struct model *p,uint32_t address)
{
 if(address==0x18000000U)return p->chip_id;
 if(address==0x180000fcU)return p->eromptr;
 if(address>=0x18110000U&&address<0x18110e00U)return p->erom[(address-0x18110000U)/4U];
 if(address==0x18102800U)return p->arm_reset;
 if(address==0x18104408U)return p->ram_clock;
 if(address==0x18104800U)return p->ram_reset;
 if(address==0x18004000U)return p->coreinfo;
 if(address==0x18004010U)return p->bankidx;
 if(address==0x18004040U){assert(p->bankidx<I9100_BCM_MAX_BANKS);return p->banks[p->bankidx];}
 assert(!"Unreviewed register/ROM/RAM read");return 0;
}
static int cmd53(void *cookie,int write,unsigned fn,uint32_t offset,int increment,uint8_t *bytes,uint32_t count)
{
 struct model *p=cookie;assert(p->locked&&fn==1U&&count==4U&&increment&&(offset&0x8000U));
 uint32_t address=((uint32_t)p->window[2]<<24)|((uint32_t)p->window[1]<<16)|
                  ((uint32_t)(p->window[0]&0x80U)<<8)|(offset&0x7fffU);
 if(write){
  assert(address==0x18004010U); /* no core resets, IRQ, remap, flash or RF */
  uint32_t v=bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
  p->bank_writes++;if(!(p->ignore_bank_restore&&v==0x101U))p->bankidx=v;
 }else{uint32_t v=word_read(p,address);for(unsigned n=0;n<4;n++)bytes[n]=(uint8_t)(v>>(8U*n));}
 return operation(p); /* side effect may happen even when the transaction fails */
}
static int send_frame(void *cookie,const uint8_t *data,uint32_t n)
{(void)cookie;(void)data;(void)n;assert(!"Unexpected F2/firmware action");return -1;}
static void core(uint16_t id,uint8_t rev,uint32_t base,uint32_t wrapper,int master)
{
 assert(m.words+5U<I9100_BCM_EROM_WORDS);
 m.erom[m.words++]=0x4bf00001U|((uint32_t)id<<8);
 m.erom[m.words++]=((uint32_t)rev<<24)|(1U<<9)|(master?(1U<<14)|(1U<<4):(1U<<19))|1U;
 if(master)m.erom[m.words++]=3U;
 m.erom[m.words++]=base|5U;m.erom[m.words++]=wrapper|(master?0xc5U:0x85U);
}
static void setup(unsigned ramrev)
{
 memset(&m,0,sizeof(m));memset(&bus,0,sizeof(bus));memset(&chip,0,sizeof(chip));
 m.chip_id=0x10034330U;m.eromptr=0x18110000U;m.coreinfo=0x30U;m.bankidx=0x101U;
 m.arm_reset=1;m.ram_clock=1;m.banks[0]=15;m.banks[1]=0x1000f;m.banks[2]=15;
 m.window[0]=0x80;m.window[1]=0x70;m.window[2]=0x16;
 core(0x800,35,0x18000000U,0x18100000U,0);
 core(0x82a,7,0x18002000U,0x18102000U,1);
 core(0x80e,(uint8_t)ramrev,0x18004000U,0x18104000U,0);
 core(0x829,12,0x18011000U,0x18111000U,0);
 m.erom[m.words++]=15U;
 struct i9100_bcm_io io={&m,lock,unlock,cmd52,cmd53,send_frame,now,delay};
 assert(!i9100_bcm_init(&bus,&io));assert(!i9100_bcm_enable(&bus,1,0x2d0,0x4330));
 assert(!i9100_bcm_chip_init(&chip,&bus));
}
static void window_original(void)
{assert(m.window[0]==0x80U&&m.window[1]==0x70U&&m.window[2]==0x16U&&!bus.window_restore_pending);}
static void finish(void)
{
 assert(!i9100_bcm_chip_ram_restore(&chip));assert(!chip.ram_selector_owned);
 assert(!i9100_bcm_chip_close(&chip));assert(!bus.chip_holds);
 assert(!i9100_bcm_enable(&bus,0,0,0));window_original();assert(!bus.enabled&&!m.locked);
}
static void decoder(void)
{
 setup(8);struct i9100_bcm_topology t;memset(&t,0xa5,sizeof(t));
 assert(!i9100_bcm_erom_decode(m.erom,m.words,&t)&&t.count==4&&t.erom_words==m.words);
 assert(t.cores[0].id==0x800&&t.cores[0].base==0x18000000U&&t.cores[1].id==0x82a);
 struct i9100_bcm_topology saved=t;
 for(unsigned n=1;n<m.words;n++){
  t=saved;assert(i9100_bcm_erom_decode(m.erom,n,&t));assert(!memcmp(&t,&saved,sizeof(t)));
 }
 uint32_t v=m.erom[m.words-1U];m.erom[m.words-1U]=0xffffffffU;
 assert(i9100_bcm_erom_decode(m.erom,m.words,&t)==I9100_BCM_CHIP_LAYOUT&&!memcmp(&t,&saved,sizeof(t)));
 m.erom[m.words-1U]=v;m.erom[1]&=~1U;
 assert(i9100_bcm_erom_decode(m.erom,m.words,&t)==I9100_BCM_CHIP_LAYOUT);m.erom[1]|=1U;
 uint32_t base=m.erom[2];m.erom[2]=0x40000005U;
 assert(i9100_bcm_erom_decode(m.erom,m.words,&t)==I9100_BCM_CHIP_LAYOUT);m.erom[2]=base;
 m.erom[2]=0x18000035U; /* variable-size descriptor, next word is consumed as size */
 assert(i9100_bcm_erom_decode(m.erom,m.words,&t));m.erom[2]=base;
 uint32_t wrapper=m.erom[3];m.erom[3]=0x18000085U;
 assert(i9100_bcm_erom_decode(m.erom,m.words,&t)==I9100_BCM_CHIP_LAYOUT);m.erom[3]=wrapper;
 assert(i9100_bcm_erom_decode(m.erom,I9100_BCM_EROM_WORDS+1U,&t)==I9100_BCM_CHIP_ARGUMENT);
 assert(i9100_bcm_erom_decode(m.erom,m.words,(struct i9100_bcm_topology *)m.erom)==I9100_BCM_CHIP_ARGUMENT);
 assert(i9100_bcm_erom_decode((uint32_t *)((uint8_t *)m.erom+1U),m.words,&t)==I9100_BCM_CHIP_ARGUMENT);
 m.words--;core(0x80e,8,0x18005000U,0x18105000U,0);m.erom[m.words++]=15U;
 assert(i9100_bcm_erom_decode(m.erom,m.words,&t)==I9100_BCM_CHIP_LAYOUT);
 setup(8);m.words--;for(unsigned n=0;n<13U;n++)core((uint16_t)(0x900U+n),1,0x18020000U+n*0x1000U,0x18120000U+n*0x1000U,0);
 m.erom[m.words++]=15U;
 assert(i9100_bcm_erom_decode(m.erom,m.words,&t)==I9100_BCM_CHIP_UNSUPPORTED);finish();
}
static void lifecycle(void)
{
 setup(8);unsigned before=m.ops;
 assert(i9100_bcm_chip_probe(&chip,0,1)==I9100_BCM_CHIP_DISABLED&&m.ops==before);
 assert(i9100_bcm_chip_probe(&chip,1,0)==I9100_BCM_CHIP_ARGUMENT&&m.ops==before);
 struct i9100_bcm_chip copy=chip;
 assert(i9100_bcm_chip_probe(&copy,1,1)==I9100_BCM_CHIP_ARGUMENT&&m.ops==before);
 assert(i9100_bcm_chip_close(&copy)==I9100_BCM_CHIP_ARGUMENT&&bus.chip_holds==1U);
 struct i9100_bcm_chip other={0};assert(i9100_bcm_chip_init(&other,&bus)==I9100_BCM_CHIP_BUSY);
 assert(i9100_bcm_enable(&bus,0,0,0)==I9100_BCM_BUSY&&m.ops==before);
 assert(i9100_bcm_protocol_start(&bus,4,3)==I9100_BCM_BUSY&&!bus.protocol_ready);
 assert(!i9100_bcm_chip_probe(&chip,1,1)&&chip.probed&&!chip.ram_ready&&!m.bank_writes);window_original();
 before=m.ops;assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_BUSY&&m.ops==before);
 m.arm_reset=0;assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_NOT_READY&&!m.bank_writes&&!chip.fault);
 m.arm_reset=1;m.ram_clock=3;assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_NOT_READY&&!m.bank_writes);
 m.ram_clock=1;m.ram_reset=1;assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_NOT_READY&&!m.bank_writes);
 m.ram_reset=0;assert(!i9100_bcm_chip_ram_probe(&chip)&&chip.ram_ready&&chip.ram_size==393216U);
 assert(chip.retention_size==131072U&&chip.bank_count==3U&&m.bankidx==0x101U&&!chip.ram_selector_owned);
 finish();
 setup(8);m.chip_id=0x00034330U;assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_ID&&!chip.probed&&!m.bank_writes);finish();
 setup(8);m.eromptr=0xffffffffU;assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_LAYOUT&&!chip.probed);finish();
 setup(8);m.step=10000000ULL;assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_TIMEOUT&&chip.fault&&!m.bank_writes);finish();
 setup(8);memset(m.erom,0,sizeof(m.erom));
 assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_LAYOUT&&chip.fault);finish();
}
static void ram_sizes(void)
{
 const unsigned revisions[]={4,7,8,12,23};
 for(unsigned n=0;n<sizeof(revisions)/sizeof(revisions[0]);n++){
  setup(revisions[n]);assert(!i9100_bcm_chip_probe(&chip,1,1));
  if(revisions[n]<=7U||revisions[n]==12U)m.coreinfo=0x400033U;
  if(revisions[n]>=23U){m.coreinfo=31U<<4;for(unsigned b=0;b<31U;b++)m.banks[b]=0;}
  assert(!i9100_bcm_chip_ram_probe(&chip));
  assert(chip.ram_size==(revisions[n]>=23U?31U*8192U:393216U));
  assert(m.bankidx==0x101U);finish();
 }
 setup(3);assert(!i9100_bcm_chip_probe(&chip,1,1));assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_UNSUPPORTED&&!m.bank_writes);finish();
 setup(12);assert(!i9100_bcm_chip_probe(&chip,1,1));m.coreinfo=15U<<20;
 assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_LAYOUT&&!chip.ram_ready&&!m.bank_writes);finish();
 setup(23);assert(!i9100_bcm_chip_probe(&chip,1,1));m.coreinfo=31U<<4;for(unsigned n=0;n<31U;n++)m.banks[n]=127;
 assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_LAYOUT&&!chip.ram_ready&&!chip.ram_selector_owned);finish();
}
static void faults(void)
{
 setup(8);unsigned start=m.ops;assert(!i9100_bcm_chip_probe(&chip,1,1));unsigned count=m.ops-start;
 for(unsigned n=1;n<=count;n++){
  setup(8);m.fail_op=m.ops+n;
  assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_IO&&chip.fault&&!chip.probed&&!m.bank_writes);
  unsigned before=m.ops;assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_IO&&m.ops==before);finish();
 }
 setup(8);assert(!i9100_bcm_chip_probe(&chip,1,1));start=m.ops;
 assert(!i9100_bcm_chip_ram_probe(&chip));count=m.ops-start;
 for(unsigned n=1;n<=count;n++){
  setup(8);assert(!i9100_bcm_chip_probe(&chip,1,1));m.fail_op=m.ops+n;
  assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_IO&&!chip.ram_ready&&!chip.ram_size&&chip.fault);
  if(chip.ram_selector_owned||bus.window_restore_pending)assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY);
  finish();assert(m.bankidx==0x101U);
 }
 setup(8);m.ignore_window_restore=1;
 assert(i9100_bcm_chip_probe(&chip,1,1)==I9100_BCM_CHIP_IO&&bus.window_restore_pending);
 assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY);
 assert(i9100_bcm_chip_ram_restore(&chip)==I9100_BCM_CHIP_IO&&bus.window_restore_pending);
 m.ignore_window_restore=0;finish();
 setup(8);assert(!i9100_bcm_chip_probe(&chip,1,1));m.ignore_bank_restore=1;
 assert(i9100_bcm_chip_ram_probe(&chip)==I9100_BCM_CHIP_IO&&chip.ram_selector_owned&&chip.cleanup_error);
 assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY);
 m.arm_reset=0;assert(i9100_bcm_chip_ram_restore(&chip)==I9100_BCM_CHIP_NOT_READY&&chip.ram_selector_owned);
 m.arm_reset=1;m.ignore_bank_restore=0;finish();assert(m.bankidx==0x101U);
}
int main(void)
{decoder();lifecycle();ram_sizes();faults();puts("PASS BCM4330 F1/DMP bounded topology/atomic publish/core aliases/EOT/CM3-reset guard/SOCRAM revisions+retention/window and bank restore/hold lifetimes/all transaction failures; no core-reset or RF actions");return 0;}
