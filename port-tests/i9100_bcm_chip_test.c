/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_wire_model.h"
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
