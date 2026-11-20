/* SPDX-License-Identifier: BSD-2-Clause
 * Actual MMC/IDMAC/guard/writer. Only register/card/FIFO/memory DMA boundary
 * is simulated; no QEMU, real cache, interrupt delivery or device claim. */
#define I9100_EMMC_WRITER_MAIN idmac_legacy_writer_main
#include "i9100_emmc_writer_test.c"
#include "i9100_emmc_idmac.h"
static uint8_t descriptors[4096] __attribute__((aligned(4096)));
static uint8_t payload[16384] __attribute__((aligned(4096)));
static struct i9100_dma_buffer desc_guard,data_guard;
static struct i9100_emmc_idmac idmac;
static struct {
 unsigned barriers,syncs,releases,locked,dma_commands,dma_reads,dma_writes,host_writes,completion_polls,delay_complete;
 unsigned fault,fail_sync;uint32_t expected_count,expected_command;
 unsigned automatic_expectation;
} dm;
enum {D_TIMEOUT=1,D_STALE_EVENT,D_SHORT_COUNTER,D_OWN_STUCK,D_DESC_CORRUPT,D_CRC,D_IDERROR,D_MODE_DROP,D_DB_DROP,D_STOP_DROP,D_LATE_DIRECTION};
static int board_check(void *v){assert(v==&dm);return 0;}
static void barrier(void *v){assert(v==&dm);dm.barriers++;}
static int buffer_lock(void *v){assert(v==&dm&&!dm.locked);dm.locked=1;return 0;}
static void buffer_unlock(void *v){assert(v==&dm&&dm.locked);dm.locked=0;}
static int sync_memory(void *v,uintptr_t cpu,uint32_t pa,uint32_t n,unsigned action)
{
 assert(v==&dm&&action>=I9100_DMA_CLEAN&&action<=I9100_DMA_BARRIER);
 assert((cpu==(uintptr_t)descriptors&&pa==0x46000000U&&n==4096U)||
        (cpu==(uintptr_t)payload&&pa==0x46001000U&&n==16384U));
 return ++dm.syncs==dm.fail_sync?-1:0;
}
static int buffer_idle(void *v)
{
 assert(v==&dm);return wm.m.regs[EM_CTRL/4]&((1U<<5)|(1U<<25))||wm.m.regs[EM_BMOD/4]&0x81U||
  wm.m.regs[EM_CMD/4]&(1U<<31)||wm.m.regs[EM_IDSTS/4]&(15U<<13)?-1:0;
}
static void release_memory(void *v,uintptr_t cpu,uint32_t pa,uint32_t bytes)
{assert(v==&dm&&(cpu==(uintptr_t)descriptors||cpu==(uintptr_t)payload)&&pa>=0x46000000&&bytes);dm.releases++;}
static void transfer_card(unsigned cmd,uint32_t lba,uint32_t bytes)
{
 assert((dm.automatic_expectation||(dm.expected_count==bytes/512U&&dm.expected_command==cmd))&&wm.m.regs[EM_DBADDR/4]==0x46000000U);
 unsigned desc_count=(bytes+4095U)/4096U,remaining=bytes;
 for(unsigned i=0;i<desc_count;i++){
  uint8_t *d=descriptors+i*16U;unsigned n=remaining>4096U?4096U:remaining;
  uint32_t flags=1U<<31|(i==0?1U<<3:0U)|(i+1U==desc_count?1U<<2:(1U<<4)|(1U<<1));
  assert(get32(d)==flags&&get32(d+4)==n&&get32(d+8)==0x46001000U+i*4096U&&
    get32(d+12)==(i+1U==desc_count?0U:0x46000000U+(i+1U)*16U));
  if(dm.fault!=D_OWN_STUCK)put32(d,flags&~(1U<<31));remaining-=n;
 }
 assert(!remaining);dm.dma_commands++;
 if(cmd==24U||cmd==25U){
  dm.dma_writes++;wm.write_commands++;wm.stored_lba=lba;wm.stored_count=bytes/512U;memcpy(wm.stored,payload,bytes);
  wm.m.state=cmd==25U?5U:4U;
 }else{
  dm.dma_reads++;for(unsigned offset=0;offset<bytes;offset+=512U){
   uint64_t sector=(uint64_t)lba+offset/512U;uint8_t *d=payload+offset;
   if(wm.stored_count&&sector>=wm.stored_lba&&sector<wm.stored_lba+wm.stored_count)
    memcpy(d,wm.stored+(sector-wm.stored_lba)*512U,512U);
   else if(disk_read(&wm.d,sector,d))memset(d,0,512U);
  }
  wm.m.state=cmd==18U?5U:4U;
 }
 wm.m.regs[EM_TCBCNT/4]=bytes-(dm.fault==D_SHORT_COUNTER?4U:0U);wm.m.regs[EM_TBBCNT/4]=bytes;
 wm.m.count=wm.m.position=0;
 if(dm.fault==D_DESC_CORRUPT)descriptors[12]^=4U;
 wm.m.regs[EM_IDSTS/4]=1U<<8|(cmd==24U||cmd==25U?1U:2U);
 if(dm.fault==D_LATE_DIRECTION){wm.m.regs[EM_IDSTS/4]=1U<<8;dm.delay_complete=3;}
 if(dm.fault==D_IDERROR)wm.m.regs[EM_IDSTS/4]|=1U<<2;
 wm.m.regs[EM_RINTSTS/4]|=1U<<3;
 if(dm.fault==D_CRC)wm.m.regs[EM_RINTSTS/4]|=1U<<7;
 if(dm.fault==D_TIMEOUT){wm.m.regs[EM_RINTSTS/4]&=~(1U<<3);wm.m.regs[EM_IDSTS/4]=1U<<13;}
 if(dm.fault==D_STALE_EVENT){wm.m.regs[EM_RINTSTS/4]&=~(1U<<3);wm.m.regs[EM_IDSTS/4]=0;}
}
static uint32_t dma_read(void *v,unsigned off)
{
 assert(v==&wm);if(off==EM_DATA&&wm.m.regs[EM_CTRL/4]&(1U<<25))assert(!"DMA must not use PIO DATA");
 if(off==EM_IDSTS&&dm.delay_complete){dm.completion_polls++;if(!--dm.delay_complete)wm.m.regs[EM_IDSTS/4]|=2U;}
 return wread(v,off);
}
static void dma_write(void *v,unsigned off,uint32_t value)
{
 assert(v==&wm);dm.host_writes++;
 if(off==EM_CTRL){
  if(dm.fault==D_MODE_DROP&&value&(1U<<25))return;
  if(dm.fault==D_STOP_DROP&&!(value&(1U<<25))&&wm.m.regs[EM_CTRL/4]&(1U<<25))return;
  wm.m.regs[off/4]=value&~7U;return;
 }
 if(off==EM_BMOD){wm.m.regs[off/4]=value&~1U;return;}
 if(off==EM_DBADDR){if(dm.fault!=D_DB_DROP)wm.m.regs[off/4]=value;return;}
 if(off==EM_CMD&&!(value&(1U<<21))&&wm.m.regs[EM_CTRL/4]&(1U<<25)){
  unsigned cmd=value&63U;assert(cmd==17U||cmd==18U||cmd==24U||cmd==25U);
  assert(wm.m.state==4U&&value&(1U<<9)&&!!(value&(1U<<10))==(cmd==24U||cmd==25U));
  wm.m.regs[EM_CMD/4]=value&~(1U<<31);wm.m.regs[EM_RINTSTS/4]|=1U<<2;wm.m.commands++;
  response(&wm.m,(4U<<9)|(1U<<8));transfer_card(cmd,wm.m.regs[EM_CMDARG/4],wm.m.regs[EM_BYTCNT/4]);return;
 }
 wwrite(v,off,value);
}
static struct i9100_emmc_writer_identity setup(const char *source)
{
 struct i9100_emmc_writer_identity id=setup_writer(source,1);memset(&dm,0,sizeof(dm));memset(&idmac,0,sizeof(idmac));
 memset(&desc_guard,0,sizeof(desc_guard));memset(&data_guard,0,sizeof(data_guard));
 wm.card.io.read32=dma_read;wm.card.io.write32=dma_write;
 struct i9100_dma_io io={&dm,buffer_lock,buffer_unlock,sync_memory,buffer_idle,release_memory};
 assert(!i9100_dma_bind(&desc_guard,&io,(uintptr_t)descriptors,0x46000000,4096));
 assert(!i9100_dma_bind(&data_guard,&io,(uintptr_t)payload,0x46001000,16384));return id;
}
static void expect(unsigned command,unsigned sectors){dm.expected_command=command;dm.expected_count=sectors;}
static void clean(void)
{
 assert(!i9100_emmc_idmac_unbind(&idmac,&wm.card));
 assert(!i9100_dma_release(&desc_guard)&&!i9100_dma_release(&data_guard)&&dm.releases==2);done();
}
struct pair_lock_model {unsigned calls,fail_at,held;};
static int pair_lock(void *v)
{
 struct pair_lock_model *l=v;assert(l&&!l->held);
 if(++l->calls==l->fail_at)return -1;l->held=1;return 0;
}
static void pair_unlock(void *v)
{struct pair_lock_model *l=v;assert(l&&l->held);l->held=0;}
static void pair_lock_io(struct pair_lock_model locks[2])
{
 desc_guard.io.cookie=&locks[0];data_guard.io.cookie=&locks[1];
 desc_guard.io.lock=data_guard.io.lock=pair_lock;desc_guard.io.unlock=data_guard.io.unlock=pair_unlock;
}
static void restore_buffer_io(void)
{
 desc_guard.io.cookie=data_guard.io.cookie=&dm;desc_guard.io.lock=data_guard.io.lock=buffer_lock;
 desc_guard.io.unlock=data_guard.io.unlock=buffer_unlock;
}
int main(int argc,char **argv)
{
 assert(argc==2);const struct i9100_emmc_idmac_io io={&dm,board_check,barrier,1};uint8_t bytes[16384],readback[16384];
 setup(argv[1]);unsigned commands=wm.m.commands;
 assert(i9100_emmc_idmac_bind(0,&idmac,&wm.card,&io,&desc_guard,&data_guard)==I9100_EMMC_UNSUPPORTED&&!wm.card.dma&&wm.m.commands==commands);done();
 /* Either allocation lock can fail. Binding and paired holds must publish
  * together, and an unbind cannot relinquish just one of its two buffers. */
 for(unsigned failure=0;failure<2;failure++){
  setup(argv[1]);assert(!i9100_dma_enable(&desc_guard,1)&&!i9100_dma_enable(&data_guard,1));
  struct pair_lock_model locks[2]={{0,0,0},{0,0,0}};locks[failure].fail_at=1;pair_lock_io(locks);
  commands=wm.m.commands;
  assert(i9100_emmc_idmac_bind(1,&idmac,&wm.card,&io,&desc_guard,&data_guard)==I9100_DMA_BUSY);
  assert(!wm.card.dma&&!idmac.binding.self&&!idmac.binding.enabled&&!desc_guard.holds&&!data_guard.holds);
  assert(!locks[0].held&&!locks[1].held&&wm.m.commands==commands);restore_buffer_io();
  assert(!i9100_dma_release(&desc_guard)&&!i9100_dma_release(&data_guard)&&dm.releases==2);done();
 }
 setup(argv[1]);assert(!i9100_emmc_idmac_bind(1,&idmac,&wm.card,&io,&desc_guard,&data_guard));
 struct pair_lock_model unlocks[2]={{0,0,0},{0,1,0}};pair_lock_io(unlocks);
 assert(i9100_emmc_idmac_unbind(&idmac,&wm.card)==I9100_DMA_BUSY);
 assert(wm.card.dma==&idmac.binding&&idmac.binding.enabled&&desc_guard.holds==1&&data_guard.holds==1);
 assert(!unlocks[0].held&&!unlocks[1].held);restore_buffer_io();clean();
 for(unsigned sectors=1;sectors<=32;sectors=sectors==1?8:32){
  struct i9100_emmc_writer_identity id=setup(argv[1]);
  assert(!i9100_emmc_idmac_bind(1,&idmac,&wm.card,&io,&desc_guard,&data_guard));
  expect(sectors==1?17:18,sectors);assert(!i9100_emmc_read(&wm.card,1,sectors,readback));
  assert(idmac.completed==1&&desc_guard.state==I9100_DMA_CPU&&data_guard.state==I9100_DMA_CPU&&!idmac.binding.active);
  /* writer open reads tables as 1-sector IO; exact transfer checks vary. */
  dm.expected_count=1;dm.expected_command=17;
  assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL+8388608ULL,32));
  for(unsigned k=0;k<sizeof(bytes);k++)bytes[k]=(uint8_t)(k*17U+3U);
  /* A write and its mandatory readback use different command directions. */
  dm.expected_count=sectors;dm.expected_command=sectors==1?24:25;
  assert(!i9100_emmc_write(&wm.card,&writer.grant,writer.grant.first_lba,sectors,bytes));
  expect(sectors==1?17:18,sectors);assert(!i9100_emmc_read(&wm.card,writer.grant.first_lba,sectors,readback)&&!memcmp(bytes,readback,sectors*512U));
  assert(dm.dma_writes==1&&dm.dma_reads>1&&writer.grant.ciu_bytes==sectors*512U&&writer.grant.host_bytes==sectors*512U);
  assert(!i9100_emmc_writer_close(&writer)&&wm.flushes==1);clean();if(sectors==32)break;
 }
 struct i9100_emmc_writer_identity original=setup(argv[1]);
 assert(!i9100_emmc_idmac_bind(1,&idmac,&wm.card,&io,&desc_guard,&data_guard));dm.automatic_expectation=1;
 assert(desc_guard.holds==1&&data_guard.holds==1&&i9100_dma_release(&desc_guard)==I9100_DMA_BUSY);
 assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&original,I9100_EMMC_DATA_VOLUME,3489792ULL+8388608ULL,32));
 for(unsigned n=0;n<sizeof(bytes);n++)bytes[n]=(uint8_t)(n*29U);
 assert(!i9100_emmc_writer_write(&writer,0,32,bytes)&&writer.verified_sectors==32&&dm.dma_writes==1);
 assert(!i9100_emmc_writer_close(&writer));clean();
 for(unsigned fault=1;fault<=11;fault++){
  setup(argv[1]);assert(!i9100_emmc_idmac_bind(1,&idmac,&wm.card,&io,&desc_guard,&data_guard));
  dm.fault=fault;wm.card.io.poll_bound=16;wm.freeze_time=1;expect(17,1);memset(readback,0xa5,512);
  int e=i9100_emmc_read(&wm.card,1,1,readback);
  if(fault==D_LATE_DIRECTION){assert(!e&&dm.completion_polls==3);clean();continue;}
  assert(e&&idmac.binding.poisoned&&!wm.card.ready);
  for(unsigned n=0;n<512;n++)assert(readback[n]==0xa5);
  assert(desc_guard.state==I9100_DMA_QUARANTINED||data_guard.state==I9100_DMA_QUARANTINED);
  commands=wm.m.commands;assert(i9100_emmc_read(&wm.card,1,1,readback)==I9100_EMMC_NO_DEVICE&&wm.m.commands==commands);
  assert(i9100_emmc_idmac_unbind(&idmac,&wm.card)!=0);assert(i9100_dma_release(&data_guard)!=0&&dm.releases==0);done();
 }
 setup(argv[1]);assert(!i9100_emmc_idmac_bind(1,&idmac,&wm.card,&io,&desc_guard,&data_guard));
 struct i9100_emmc_idmac_binding copy=idmac.binding;wm.card.dma=&copy;
 assert(i9100_emmc_transport_check(&wm.card)==I9100_EMMC_OWNER);wm.card.dma=&idmac.binding;clean();
 for(unsigned bad=0;bad<5;bad++){
  setup(argv[1]);struct i9100_emmc_idmac_io wrong=io;
  if(bad==0)wrong.descriptors_normal_nc=0;
  else if(bad==1)data_guard.physical=desc_guard.physical;
  else if(bad==2)wm.card.hcon|=1U<<16;
  else if(bad==3)data_guard.bytes=32768;
  else{assert(!i9100_dma_hold(&data_guard));}
  assert(i9100_emmc_idmac_bind(1,&idmac,&wm.card,&wrong,&desc_guard,&data_guard)!=0&&!wm.card.dma&&!idmac.binding.self);
  if(bad==4)assert(!i9100_dma_unhold(&data_guard));done();
 }
 setup(argv[1]);assert(!i9100_emmc_idmac_bind(1,&idmac,&wm.card,&io,&desc_guard,&data_guard));expect(17,1);
 dm.fail_sync=dm.syncs+1;assert(i9100_emmc_read(&wm.card,1,1,readback)==I9100_DMA_IO&&idmac.binding.poisoned&&dm.dma_commands==0);
 assert(data_guard.state==I9100_DMA_QUARANTINED&&i9100_dma_release(&data_guard)!=0);done();
 puts("PASS actual IDMAC32/core/guard/writer: 1/8/32sectors/>4GiB,4KiB descriptors,PA/length/next,DTO+direction+two counters+OWN return,NC barrier/CPU handoff,11faults/late events/stoppedclock/quarantine/no PIOretry; hardware facade only");return 0;
}
