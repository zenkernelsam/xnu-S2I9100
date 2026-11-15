/* SPDX-License-Identifier: BSD-2-Clause
 * Execute the real native adapter against an IOKit allocation/map facade.
 * PL330 bytecode/data fidelity belongs to the independent audio VM suite;
 * this model checks resource order, default OFF and failure ownership. */
#include "i9100_audio_native.h"
#include "i9100_dma_native.h"
#include "offline-native-sdk/i9100_model_iokit.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
struct i9100_peripheral_native {i9100_gpio_broker gpio;i9100_clock_gates clocks;};
struct i9100_dma_native {
 i9100_dma_buffer buffer;unsigned index,locked;
 void *engine;int (*idle)(void *);
};
static struct {
 unsigned allocations,fail_allocation,contexts,locks,descriptors,maps,dma_count;
 unsigned gpio_locked,clock_locked,bus_locked,broker_writes,fail_broker_write,sticky_gpio,sticky_clock;
 unsigned mute_fail,transactions,dma_releases,sync_calls,fail_sync,kill_stuck,commands;
 uint32_t pdma[1024],i2s[1024],gic[1024],gpio[3][1024],cmu[16384];
 uint8_t codec[5][256],address[5];uint64_t ns;bool interrupts;
 uint32_t previous_debug;
 uint32_t previous_mode;
 unsigned board_mode,board_bus_owned,board_bus_fail,board_close_fail,board_tx_fail,board_stall_power;
 unsigned check_mclk_silence,extra_contexts;uint32_t previous_mclk;
 unsigned board_aa_unlocked;
 uint32_t pmu[4096],chip[1024],previous_power;uint64_t power_ready_at,vref_started;
} m;
static uint32_t board_audss[1024] __attribute__((aligned(4096)));
static uint32_t board_i2s[1024] __attribute__((aligned(4096)));
static uint32_t *i2s_regs(){return m.board_mode?board_i2s:m.i2s;}
static i9100_peripheral_native parent;
static i9100_audss audss;
static i9100_mc1n2 codec;
static uint8_t allocation_bytes[3][16384] __attribute__((aligned(4096)));
static bool allocation_fails(){return ++m.allocations==m.fail_allocation;}
void *IOMalloc(size_t bytes){if(allocation_fails())return 0;m.contexts++;return calloc(1,bytes);}
void IOFree(void *p,size_t){assert(m.contexts);m.contexts--;free(p);}
IOLock *IOLockAlloc(){if(allocation_fails())return 0;m.locks++;return new IOLock{false};}
void IOLockFree(IOLock *p){assert(!p->held&&m.locks);m.locks--;delete p;}
bool IOLockTryLock(IOLock *p){assert(p);if(p->held)return false;p->held=true;return true;}
void IOLockUnlock(IOLock *p){assert(p->held);p->held=false;}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t pa,size_t bytes,unsigned direction)
{assert((bytes==4096||(m.board_mode&&pa==0x10020000U&&bytes==16384))&&direction==kIODirectionInOut);if(allocation_fails())return 0;m.descriptors++;return new IOMemoryDescriptor{pa,bytes};}
IOMemoryMap *IOMemoryDescriptor::map(unsigned flags)
{
 assert(flags==kIOMapInhibitCache);if(allocation_fails())return 0;uint32_t *regs=0;
 if(physical==0x12680000U)regs=m.pdma;else if(physical==0x03830000U)regs=i2s_regs();
 else if(physical==0x10490000U)regs=m.gic;
 else if(m.board_mode&&physical==0x10020000U)regs=m.pmu;
 else if(m.board_mode&&physical==0x10000000U)regs=m.chip;
 else if(m.board_mode&&physical==0x03810000U)regs=board_audss;
 else assert(!"unexpected native physical map");
 m.maps++;return new IOMemoryMap{(uintptr_t)regs};
}
void IOMemoryDescriptor::release(){assert(m.descriptors);m.descriptors--;delete this;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(m.maps);m.maps--;delete this;}
void OSSynchronizeIO()
{
 if(m.board_mode){
  if(m.check_mclk_silence&&(m.previous_mclk&1U)&&!(m.pmu[0xa00/4]&1U))
   assert((m.codec[3][78]&2U)&&(m.codec[2][3]&0x3fU)==0x3fU&&m.transactions>0);
  m.previous_mclk=m.pmu[0xa00/4];
  if(m.pmu[0x3cc0/4]!=m.previous_power){m.previous_power=m.pmu[0x3cc0/4];m.power_ready_at=m.ns+10000ULL;}
  if(!m.board_stall_power&&m.ns>=m.power_ready_at)m.pmu[0x3cc4/4]=(m.pmu[0x3cc4/4]&~7U)|(m.previous_power&7U);
  int access=(m.pmu[0x3cc4/4]&7U)==7U?PROT_READ|PROT_WRITE:PROT_NONE;
  assert(!mprotect(board_audss,4096,access)&&!mprotect(board_i2s,4096,access));
  if((m.pmu[0x3cc4/4]&7U)!=7U)return; /* GPIO/AUDSS/I2S are inaccessible while off. */
 }
 uint32_t *port=i2s_regs();
 if((m.previous_mode&0x1800U)==0x1800U&&(port[1]&0x1800U)!=0x1800U)
  for(unsigned n=0;n<3;n++)assert(((m.gpio[2][0]>>(n*4U))&15U)!=2U);
 m.previous_mode=port[1];
 /* Memory-backed facade: acknowledge W1C and an observed KILL request. GO
  * completion is deliberately harness-driven, not a second encoder oracle. */
 if(m.pdma[0x2c/4]){m.pdma[0x24/4]&=~m.pdma[0x2c/4];m.pdma[0x2c/4]=0;}
 if(m.pdma[0xd08/4]!=m.previous_debug){m.previous_debug=m.pdma[0xd08/4];
  if(((m.previous_debug>>16)&255U)==1U){assert(!(port[0]&7U));m.commands++;
   if(!m.kill_stuck)m.pdma[0x100/4]=0;}}
}
void IODelay(unsigned us){m.ns+=(uint64_t)us*1000;}
extern "C" int ml_get_interrupts_enabled(void){return m.interrupts?1:0;}
extern "C" uint64_t mach_absolute_time(void){return m.ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
static int buffer_lock(void *c){i9100_dma_native *n=(i9100_dma_native *)c;if(n->locked)return -1;n->locked=1;return 0;}
static void buffer_unlock(void *c){i9100_dma_native *n=(i9100_dma_native *)c;assert(n->locked);n->locked=0;}
static int buffer_idle(void *c){i9100_dma_native *n=(i9100_dma_native *)c;return n->idle(n->engine);}
static int buffer_sync(void *c,uintptr_t cpu,uint32_t pa,uint32_t bytes,unsigned action)
{i9100_dma_native *n=(i9100_dma_native *)c;assert(n->locked&&cpu==n->buffer.cpu&&pa==n->buffer.physical&&bytes==n->buffer.bytes&&action>=1&&action<=4);
 return ++m.sync_calls==m.fail_sync?-1:0;}
static void buffer_release(void *c,uintptr_t,uint32_t,uint32_t)
{i9100_dma_native *n=(i9100_dma_native *)c;assert(!n->locked&&!m.pdma[0x100/4]&&!(i2s_regs()[0]&7U));
 assert(m.cmu[0xc940/4]&1U);assert(m.maps==(m.board_mode?8U:3U)&&m.contexts==(m.board_mode?2U:1U)+m.extra_contexts);m.dma_releases++;}
int i9100_dma_native_allocate(int enabled,uint32_t bytes,void *engine,int (*idle)(void *),i9100_dma_native **out)
{
 assert(enabled==1&&bytes>=4096&&bytes<=16384&&!(*out));if(allocation_fails())return I9100_DMA_IO;
 unsigned index=0;for(unsigned n=0;n<3;n++)if(!((m.dma_count>>n)&1U)){index=n;break;}
 i9100_dma_native *a=new i9100_dma_native{};a->index=index;a->engine=engine;a->idle=idle;m.dma_count|=1U<<index;
 i9100_dma_io io={a,buffer_lock,buffer_unlock,buffer_sync,buffer_idle,buffer_release};
 assert(!i9100_dma_bind(&a->buffer,&io,(uintptr_t)allocation_bytes[index],0x41000000U+index*0x10000U,bytes));*out=a;return 0;
}
i9100_dma_buffer *i9100_dma_native_buffer(i9100_dma_native *a){return a?&a->buffer:0;}
int i9100_dma_native_destroy(i9100_dma_native **owner)
{int e=i9100_dma_release(&(*owner)->buffer);if(e)return e;m.dma_count&=~(1U<<(*owner)->index);delete *owner;*owner=0;return 0;}
i9100_gpio_broker *i9100_peripheral_native_gpio(i9100_peripheral_native *p){return &p->gpio;}
i9100_clock_gates *i9100_peripheral_native_clocks(i9100_peripheral_native *p){return &p->clocks;}
static int gpio_lock(void *){if(m.gpio_locked)return -1;m.gpio_locked=1;return 0;}
static void gpio_unlock(void *){assert(m.gpio_locked);m.gpio_locked=0;}
static uint32_t gpio_read(void *,unsigned region,unsigned off){assert(m.gpio_locked&&region<3&&off<4096);
 if(m.board_mode&&region==2U)assert((m.pmu[0x3cc4/4]&7U)==7U);return m.gpio[region][off/4];}
static void gpio_write(void *,unsigned region,unsigned off,uint32_t value)
{assert(m.gpio_locked&&region<3&&off<4096);if(m.board_mode&&region==2U)assert((m.pmu[0x3cc4/4]&7U)==7U);
 if(++m.broker_writes==m.fail_broker_write||m.sticky_gpio)return;
 if(region==2U&&!off)for(unsigned n=0;n<3;n++)if(((value>>(n*4U))&15U)==2U)assert((i2s_regs()[1]&0x1800U)==0x1800U);
 m.gpio[region][off/4]=value;}
static int clock_lock(void *){if(m.clock_locked)return -1;m.clock_locked=1;return 0;}
static void clock_unlock(void *){assert(m.clock_locked);m.clock_locked=0;}
static uint32_t clock_read(void *,unsigned off){assert(m.clock_locked&&off<65536);return m.cmu[off/4];}
static void clock_write(void *,unsigned off,uint32_t value)
{assert(m.clock_locked&&off<65536);if(++m.broker_writes==m.fail_broker_write||m.sticky_clock)return;m.cmu[off/4]=value;}
static unsigned bank(unsigned port){switch(port){case 1:return 1;case 12:return 2;case 14:return 3;case 16:return 4;default:return 0;}}
static int bus_lock(void *){if(m.bus_locked)return -1;m.bus_locked=1;return 0;}
static void bus_unlock(void *){assert(m.bus_locked);m.bus_locked=0;}
static uint64_t bus_now(void *){return m.ns;}
static int bus_delay(void *,uint32_t us){IODelay(us);return 0;}
static void board_codec_write(unsigned reg,uint8_t value)
{
 if(reg==0U&&value==1U){uint8_t id=m.codec[0][8];memset(m.codec,0,sizeof(m.codec));m.codec[0][8]=id;
  m.codec[1][0]=0x10;m.codec[1][1]=id==0x78U?7U:6U;m.codec[1][3]=0x1f;m.codec[3][15]=1;
  m.codec[1][14]=id==0x78U?1U:0U;m.board_aa_unlocked=0;
  m.codec[2][2]=15;m.codec[2][3]=m.codec[2][4]=0xff;m.codec[3][78]=2;}
 unsigned index=bank(reg);
 if(reg==60U&&value==0xd9U&&m.codec[0][8]==0x78U)m.board_aa_unlocked=1;
 if(index)m.address[index]=value;
 else if((index=bank(reg-1U))){unsigned address=m.address[index];
  if(index==2U&&address==2U&&m.codec[0][8]==0x78U&&!m.board_aa_unlocked)return;
  if(index==2U&&address==2U&&(m.codec[2][2]&1U)&&!(value&1U))m.vref_started=m.ns;
  m.codec[index][address]=value;
 }else m.codec[0][reg]=value;
}
static uint8_t board_codec_read(unsigned reg)
{
 unsigned index=bank(reg-1U);if(!index)return m.codec[0][reg];
 if(index==2U&&m.address[index]==47U){uint8_t ready=0;
  if(!(m.codec[2][2]&8U))ready|=0x80U;
  if(!(m.codec[2][2]&1U)&&m.ns>=m.vref_started+15000000ULL)ready|=0x40U;
  return ready;}
 return m.codec[index][m.address[index]];
}
static int bus_transfer(void *,const i9100_i2c_message *msgs,unsigned count,unsigned *done)
{
 assert(m.bus_locked&&msgs[0].address==0x3a);m.transactions++;*done=0;if(m.mute_fail&&m.transactions>6U)return -1;
 if(m.board_mode){
  assert(m.board_bus_owned);
  if(count==1){for(unsigned n=0;n<msgs[0].length;n+=2)board_codec_write(msgs[0].bytes[n]>>1,msgs[0].bytes[n+1]);}
  else{assert(count==2&&msgs[1].read);msgs[1].bytes[0]=board_codec_read(msgs[0].bytes[0]>>1);}
  if(m.transactions==m.board_tx_fail)return -1;*done=count;return 0;
 }
 if(count==1){for(unsigned n=0;n<msgs[0].length;n+=2){unsigned reg=msgs[0].bytes[n]>>1,index=bank(reg);
   if(index)m.address[index]=msgs[0].bytes[n+1];else if((index=bank(reg-1)))m.codec[index][m.address[index]]=msgs[0].bytes[n+1];
   else m.codec[0][reg]=msgs[0].bytes[n+1];}}
 else{unsigned reg=msgs[0].bytes[0]>>1,index=bank(reg-1);assert(count==2&&msgs[1].read);
  msgs[1].bytes[0]=index?m.codec[index][m.address[index]]:m.codec[0][reg];}
 *done=count;return 0;
}
static int borrowed_clock_lease(void *){assert(m.bus_locked);return 0;} /* facade assumes the external MCLK lease */
static int borrowed_clock_idle(void *)
{assert(m.bus_locked);if(m.i2s[0]&7U)return -1;for(unsigned n=0;n<8;n++)if(m.pdma[0x100/4+n*2]&15U)return -1;return 0;}
static void setup()
{
 assert(!m.contexts&&!m.locks&&!m.descriptors&&!m.maps&&!m.dma_count);
 memset(&m,0,sizeof(m));memset(&parent,0,sizeof(parent));memset(&audss,0,sizeof(audss));memset(&codec,0,sizeof(codec));m.interrupts=true;
 i9100_gpio_io gi={0,gpio_read,gpio_write,gpio_lock,gpio_unlock};i9100_clock_io ci={0,clock_read,clock_write,clock_lock,clock_unlock};
 assert(!i9100_gpio_broker_init(&parent.gpio,&gi,i9100_u1_banks,I9100_BANK_COUNT));assert(!i9100_gpio_broker_enable(&parent.gpio,1));
 assert(!i9100_clock_init(&parent.clocks,&ci,i9100_u1_gates,I9100_GATE_COUNT));assert(!i9100_clock_enable(&parent.clocks,1));
 audss.active=audss.domain_owned=audss.enabled=1;audss.rates.bus=24000000;audss.rates.i2s=24000000;
 i9100_peripheral_i2c io={0,bus_transfer,bus_lock,bus_unlock,bus_now,bus_delay};assert(!i9100_mc1n2_init(&codec,&io));
 codec.enabled=codec.cold_owned=1;codec.cold_stage=5;codec.id=0x79;codec.mclk_hz=24000000;codec.sample_rate=48000;
 codec.clock_owned=codec.clock_active=1;codec.clock_stage=4;
 codec.clock_guard.cookie=&m;codec.clock_guard.lease_valid=borrowed_clock_lease;codec.clock_guard.consumers_idle=borrowed_clock_idle;
 codec.clock_saved_pad=0xae;codec.clock_saved_fs=0xa0;
 m.codec[0][8]=0x79;m.codec[1][12]=0x5e;m.codec[4][137]=0x4c;m.codec[4][135]=0x20;m.codec[3][78]=2;m.codec[2][3]=0x3f;
 const uint32_t id[2]={0x41330,0xb105f00d};
 for(unsigned which=0;which<2;which++)for(unsigned n=0;n<4;n++)m.pdma[((which?0xff0:0xfe0)+4*n)/4]=(id[which]>>(8*n))&255U;
 m.pdma[0xe00/4]=1U|(7U<<4)|(31U<<12)|(31U<<17);
 m.cmu[0xc940/4]=1U<<8; /* The external SD IP clock is foreign and must survive. */
}
static int open(i9100_audio_native **out)
{return i9100_audio_native_open(1,&parent,&audss,&codec,48000,256,1536000,1,out);}
static void released()
{
 assert(!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!m.dma_count);
 assert(m.cmu[0xc940/4]==(1U<<8)&&!parent.clocks.gates[I9100_GATE_PDMA0].refs);
 for(unsigned n=0;n<7;n++)assert(!parent.gpio.banks[I9100_GPZ].owners[n]);
 assert(audss.active&&audss.domain_owned&&codec.enabled&&codec.cold_owned);
 assert((m.codec[2][3]&0x3f)==0x3f&&(m.codec[3][78]&2U));
 for(unsigned n=0;n<1024;n++)assert(!m.gic[n]);
}
int main()
{
 i9100_audio_native *a=0;setup();assert(i9100_audio_native_open(0,0,0,0,0,0,0,0,&a)==I9100_PCM_DISABLED&&!m.allocations&&!m.transactions);
 assert(i9100_audio_native_open(1,&parent,&audss,&codec,48000,256,1536000,0,&a)==I9100_PCM_ARGUMENT&&!m.allocations);
 m.codec[1][12]=0xae;assert(open(&a)==I9100_MC_NOT_READY&&!a);released();
 setup();m.codec[4][137]=0x1c;assert(open(&a)==I9100_MC_NOT_READY&&!a);released();
 setup();m.codec[4][135]=0x21;assert(open(&a)==I9100_MC_NOT_READY&&!a);released();
 setup();m.gpio[2][0]=0x222;assert(open(&a)==I9100_I2S_CLOCK&&!a);released();
 setup();m.i2s[0]=1;assert(open(&a)==I9100_I2S_BUSY&&!a);m.i2s[0]=0;released();
 setup();m.gic[0x108/4]=1U<<3;assert(open(&a)==I9100_PCM_BUSY&&!a&&!parent.clocks.gates[I9100_GATE_PDMA0].refs);m.gic[0x108/4]=0;released();
 setup();m.pdma[0x108/4]=1;assert(open(&a)==I9100_PCM_BUSY&&a); /* another channel must never be killed */
 assert(!m.commands);m.pdma[0x108/4]=0;assert(!i9100_audio_native_close(&a));released();
 setup();assert(!open(&a)&&a&&m.contexts==1&&m.maps==3&&m.dma_count==7);
 unsigned allocation_count=m.allocations,transactions=m.transactions;
 assert(open(&a)==I9100_PCM_BUSY&&m.allocations==allocation_count&&m.transactions==transactions);
 int16_t pcm[512];for(unsigned n=0;n<512;n++)pcm[n]=(int16_t)(n*127U);
 assert(!i9100_audio_native_queue(a,pcm,256));assert(!i9100_audio_native_start(a));
 assert(i9100_mc1n2_clock_stop(&codec)==I9100_MC_BUSY&&codec.clock_owned); /* borrowing must prevent early clock shutdown */
 m.pdma[0x100/4]=1;uint32_t completed=77;assert(i9100_audio_native_service(a,&completed)==I9100_PCM_BUSY&&!completed);
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;
 assert(!i9100_audio_native_service(a,&completed)&&completed==1);
 i9100_pcm_status status;assert(!i9100_audio_native_status(a,&status)&&status.completed_frames==256&&status.held_mask==7);
 assert(!i9100_audio_native_close(&a)&&!a&&m.dma_releases==3);released();
 assert(!i9100_mc1n2_clock_stop(&codec)&&!codec.clock_owned&&codec.cold_owned&&m.codec[1][12]==0xae);
 for(unsigned point=1;point<=allocation_count;point++){
  setup();m.fail_allocation=point;assert(open(&a)!=0&&!a);released();
 }
 /* A mute failure after clock proof retains provider context even before
  * DMA allocation, until explicit close verifies silence. */
 setup();m.mute_fail=1;assert(open(&a)!=0&&a&&m.contexts==1);assert(i9100_audio_native_start(a)==I9100_PCM_DISABLED);
 assert(i9100_audio_native_close(&a)!=0&&a);m.mute_fail=0;assert(!i9100_audio_native_close(&a));released();
 setup();assert(!open(&a));assert(!i9100_audio_native_start(a));m.pdma[0x100/4]=1;m.kill_stuck=1;
 assert(i9100_audio_native_close(&a)!=0&&a&&m.dma_count==7&&m.dma_releases==0);
 assert(parent.clocks.gates[I9100_GATE_PDMA0].refs==1&&m.maps==3);
 assert(i9100_audio_native_service(a,&completed)==I9100_PCM_DISABLED&&!completed);
 assert(i9100_audio_native_queue(a,pcm,256)==I9100_PCM_DISABLED);
 m.kill_stuck=0;m.pdma[0x100/4]=0;assert(!i9100_audio_native_close(&a));released();
 setup();assert(!open(&a));m.sticky_gpio=1;
 assert(i9100_audio_native_close(&a)==I9100_PCM_IO&&a&&!m.dma_releases&&parent.gpio.banks[I9100_GPZ].owners[0]&&m.maps==3);
 assert(!i9100_audio_native_status(a,&status)&&status.cleanup_error==I9100_GPIO_IO&&status.held_mask==7);
 assert((m.i2s[1]&0x1800U)==0x1800U); /* failed pad stop cannot restore AP-master mode */
 assert(i9100_audio_native_start(a)==I9100_PCM_DISABLED);m.sticky_gpio=0;assert(!i9100_audio_native_close(&a));released();
 setup();assert(!open(&a));m.sticky_clock=1;
 assert(i9100_audio_native_close(&a)==I9100_CLOCK_IO&&a&&m.dma_releases==3&&parent.clocks.gates[I9100_GATE_PDMA0].refs==1);
 m.sticky_clock=0;assert(!i9100_audio_native_close(&a));released();
 setup();assert(!open(&a));m.fail_sync=m.sync_calls+1;
 assert(i9100_audio_native_close(&a)!=0&&a&&m.dma_releases==0&&m.dma_count==7&&m.maps==3);
 assert(!i9100_audio_native_close(&a));released();
 setup();assert(!open(&a));m.interrupts=false;transactions=m.transactions;
 assert(i9100_audio_native_start(a)==I9100_PCM_BUSY&&m.transactions==transactions);
 assert(i9100_audio_native_close(&a)==I9100_PCM_BUSY&&a);m.interrupts=true;assert(!i9100_audio_native_close(&a));released();
 for(unsigned point=1;point<=8U;point++){
  setup();m.fail_broker_write=point;assert(open(&a)!=0&&!a);released();
 }
 setup();i9100_gpio_lease board_pins={};assert(!i9100_gpio_claim(&parent.gpio,I9100_GPZ,0x7fU,&board_pins));
 assert(!i9100_audio_native_open_with_pins(1,&parent,&audss,&codec,&board_pins,48000,64,1536000,1,&a));
 assert(!i9100_audio_native_close(&a)&&board_pins.active&&parent.gpio.banks[I9100_GPZ].owners[0]&&m.gpio[2][0]==0);
 assert(!i9100_mc1n2_clock_stop(&codec));assert(!i9100_gpio_release(&board_pins,I9100_GPIO_RESTORE));released();
 puts("PASS real native audio/host IOKit: clock4c+FS/AP pad directions/busy port/slave-before-mux/input-before-restore/14 allocation failures/foreign channel/borrowed providers/late close faults/explicit recovery");return 0;
}
