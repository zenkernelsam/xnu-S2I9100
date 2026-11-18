/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_board_native.h"
#include "i9100_audio_gated.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
enum {PMU,CHIP,AUDSS,I2S,PDMA,MAPS,BUILDING=1,LIVE=2,CLOSING=3};
struct i9100_audio_board_native {
 i9100_peripheral_native *parent;i9100_audio_board_profile profile;
 i9100_native_parent_hold parent_hold;i9100_native_transport_lease bus_lease;
 IOMemoryDescriptor *memory[MAPS];IOMemoryMap *mapping[MAPS];volatile uint8_t *regs[MAPS];
 IOLock *lifecycle,*pmu_lock,*audss_lock;
 i9100_audio_mclk mclk;i9100_audio_domain domain;i9100_audss audss;
 i9100_peripheral_i2c bus;i9100_mc1n2 codec;i9100_gpio_lease pins;
 i9100_mc1n2_output_lease output;
 i9100_audio_board_worker_lease *worker_owner;uint32_t worker_generation;
 i9100_audio_native *pcm;
 i9100_audio_gated *gated;
 i9100_pcm_status last_pcm;uint32_t last_pcm_valid;
 uint32_t phase,stage,temp_owned,bus_owned,worker_holds;int32_t error;
};
static int take(IOLock *lock){return ml_get_interrupts_enabled()&&IOLockTryLock(lock)?0:-1;}
static uint32_t rd(i9100_audio_board_native *a,unsigned map,unsigned off)
{OSSynchronizeIO();uint32_t value=*(volatile uint32_t *)(a->regs[map]+off);OSSynchronizeIO();return value;}
static void wr(i9100_audio_board_native *a,unsigned map,unsigned off,uint32_t value)
{OSSynchronizeIO();*(volatile uint32_t *)(a->regs[map]+off)=value;OSSynchronizeIO();}
static int pmu_take(void *c){return take(((i9100_audio_board_native *)c)->pmu_lock);}
static void pmu_drop(void *c){IOLockUnlock(((i9100_audio_board_native *)c)->pmu_lock);}
static int aud_take(void *c){return take(((i9100_audio_board_native *)c)->audss_lock);}
static void aud_drop(void *c){IOLockUnlock(((i9100_audio_board_native *)c)->audss_lock);}
static uint32_t pmu_read(void *c,unsigned off){return rd((i9100_audio_board_native *)c,PMU,off);}
static void pmu_write(void *c,unsigned off,uint32_t v){wr((i9100_audio_board_native *)c,PMU,off,v);}
static uint32_t aud_read(void *c,unsigned off){return rd((i9100_audio_board_native *)c,AUDSS,off);}
static void aud_write(void *c,unsigned off,uint32_t v){wr((i9100_audio_board_native *)c,AUDSS,off,v);}
static uint64_t now(void *){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
static int delay(void *,uint32_t us){if(!ml_get_interrupts_enabled()||us>100000U)return -1;IODelay(us);return 0;}
static int lease_valid(void *c)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;
 /* plat/cpu.h EXYNOS_CPU_MASK, not an invented revision mask. */
 return a->profile.exclusive_u1_rails_clocks_verified==1&&
  (rd(a,CHIP,0)&0xfffe0000U)==(0x43210000U&0xfffe0000U)?0:-1;
}
static int ap_idle(void *c)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;
 if((rd(a,PMU,0x3cc4U)&7U)==7U){
  if(rd(a,I2S,0)&(7U|(1U<<18)|(1U<<25)|(1U<<23)|(1U<<16)))return -1;
 }
 i9100_clock_gates *clocks=i9100_peripheral_native_clocks(a->parent);
 if(!clocks||clocks->io.lock(clocks->io.cookie))return -1;
 uint32_t gate=clocks->io.read32(clocks->io.cookie,0xc940U);
 bool retained=clocks->gates[I9100_GATE_PDMA0].refs!=0;
 clocks->io.unlock(clocks->io.cookie);
 if(!(gate&1U))return retained?-1:0; /* Do not read a gated-off PDMA register bank. */
 if(rd(a,PDMA,0xd00U)&1U||rd(a,PDMA,0x30U)||rd(a,PDMA,0x34U)||rd(a,PDMA,0x20U))return -1;
 for(unsigned n=0;n<8U;n++)if(rd(a,PDMA,0x100U+n*8U)&15U)return -1;
 return 0;
}
static int owners_idle(void *c)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;
 return a->pcm||a->gated||a->codec.enabled||a->codec.cold_owned||a->codec.clock_owned?-1:ap_idle(c);
}
static int transition_get(void *c)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;
 if(a->temp_owned||lease_valid(c)||ap_idle(c))return -1;
 /* Linux6.1 names C93C E4X12_GATE_IP_MAUDIO. Exynos4210 uses direct
  * CFG/STATUS power sequencing; reserve the transition, do not write it. */
 a->temp_owned=1;return 0;
}
static int transition_put(void *c){((i9100_audio_board_native *)c)->temp_owned=0;return 0;}
static int confirm_bus(void *c)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;
 if((rd(a,PMU,0x3cc4U)&7U)!=7U)return -1;
 (void)rd(a,AUDSS,0);return 0;
}
static int domain_get(void *c,unsigned *owned)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;int e=i9100_audio_domain_acquire(&a->domain);
 *owned=a->domain.owned;return e;
}
static int domain_put(void *c){return i9100_audio_domain_release(&((i9100_audio_board_native *)c)->domain);}
static int clock_lease(void *c)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;uint32_t hz=0;
 if((rd(a,PMU,0x3cc4U)&7U)!=7U||!a->audss.active||!a->domain.ready||
    i9100_audio_mclk_rate(&a->mclk,&hz)||hz!=24000000U)return -1;
 i9100_gpio_broker *gpio=i9100_peripheral_native_gpio(a->parent);
 if(!a->pins.active||gpio->io.lock(gpio->io.cookie))return -1;
 uint32_t con=gpio->io.read32(gpio->io.cookie,2,0);bool owned=true;
 for(unsigned n=0;n<7U;n++)if(gpio->banks[I9100_GPZ].owners[n]!=a->pins.generation)owned=false;
 gpio->io.unlock(gpio->io.cookie);if(!owned)return -1;
 uint32_t mode=rd(a,I2S,4);
 for(unsigned n=0;n<3U;n++){
  unsigned function=(con>>(n*4U))&15U;
  if(function&&!(function==2U&&(mode&0x1800U)==0x1800U))return -1;
 }
 return 0;
}
static int output_stream(void *c,uint32_t rate)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;
 if(a->gated)return i9100_audio_gated_output_ready(a->gated,rate);
 return a->pcm?i9100_audio_native_output_ready(a->pcm,rate):I9100_PCM_DISABLED;
}
static int output_idle(void *c)
{
 i9100_audio_board_native *a=(i9100_audio_board_native *)c;
 if(a->gated)return i9100_audio_gated_consumers_idle(a->gated);
 return a->pcm?i9100_audio_native_consumers_idle(a->pcm):ap_idle(c);
}
static bool map(i9100_audio_board_native *a,unsigned n,uint32_t pa,uint32_t bytes)
{
 a->memory[n]=IOMemoryDescriptor::withPhysicalAddress(pa,bytes,kIODirectionInOut);
 a->mapping[n]=a->memory[n]?a->memory[n]->map(kIOMapInhibitCache):0;
 a->regs[n]=a->mapping[n]?(volatile uint8_t *)a->mapping[n]->getAddress():0;return a->regs[n]!=0;
}
static void dispose(i9100_audio_board_native *a)
{
 for(unsigned n=0;n<MAPS;n++){if(a->mapping[n])a->mapping[n]->release();if(a->memory[n])a->memory[n]->release();}
 if(a->lifecycle)IOLockFree(a->lifecycle);if(a->pmu_lock)IOLockFree(a->pmu_lock);if(a->audss_lock)IOLockFree(a->audss_lock);
 if(a->parent_hold.owned)(void)i9100_peripheral_native_hold_put(&a->parent_hold);
 IOFree(a,sizeof(*a));
}
static int cleanup(i9100_audio_board_native *a)
{
 a->phase=CLOSING;int e=0;
 if(a->gated){if(!i9100_audio_gated_status(a->gated,&a->last_pcm))a->last_pcm_valid=1;
  e=i9100_audio_gated_close(&a->gated);if(e)return e;}
 if(a->pcm){if(!i9100_audio_native_status(a->pcm,&a->last_pcm))a->last_pcm_valid=1;
  e=i9100_audio_native_close(&a->pcm);if(e)return e;}
 if(a->output.owned){e=i9100_mc1n2_output_stop(&a->output);if(e)return e;}
 if(a->codec.clock_owned){e=i9100_mc1n2_clock_stop(&a->codec);if(e)return e;}
 if(a->codec.cold_owned){e=i9100_mc1n2_cold_stop(&a->codec);if(e)return e;}
 if(a->codec.enabled){e=i9100_mc1n2_enable(&a->codec,0);if(e)return e;}
 if(a->bus_owned){e=i9100_peripheral_native_transport_close(&a->bus_lease);if(e)return e;a->bus_owned=0;}
 if(a->pins.active){e=i9100_gpio_release(&a->pins,I9100_GPIO_RESTORE);if(e)return e;}
 if(a->audss.domain_owned){e=i9100_audss_release(&a->audss);if(e)return e;}
 if(a->domain.owned){e=i9100_audio_domain_release(&a->domain);if(e)return e;}
 if(a->mclk.owned){e=i9100_audio_mclk_release(&a->mclk);if(e)return e;}
 return 0;
}
int i9100_audio_board_open(int enabled,i9100_peripheral_native *parent,const i9100_audio_board_profile *profile,i9100_audio_board_native **out)
{
 if(!out)return I9100_AP_ARGUMENT;if(*out)return I9100_AP_BUSY;if(enabled!=1)return I9100_AP_DISABLED;
 if(!parent||!profile||!ml_get_interrupts_enabled()||profile->exclusive_u1_rails_clocks_verified!=1||
    profile->verified_rates.xtal!=24000000U||!profile->verified_i2c6_pclk_hz||profile->use_config>1U||
    (profile->sample_rate!=44100U&&profile->sample_rate!=48000U)||profile->period_frames<16U||profile->period_frames>4096U||
    profile->use_gated_dma>1U||(profile->use_gated_dma&&profile->period_frames>512U))
  return I9100_AP_ARGUMENT;
 i9100_gpio_broker *gpio=i9100_peripheral_native_gpio(parent);i9100_clock_gates *clocks=i9100_peripheral_native_clocks(parent);
 if(!gpio||!clocks||!gpio->enabled||!clocks->enabled||gpio->count!=I9100_BANK_COUNT||clocks->count!=I9100_GATE_COUNT)return I9100_AP_DISABLED;
 i9100_audio_board_native *a=(i9100_audio_board_native *)IOMalloc(sizeof(*a));if(!a)return I9100_AP_IO;
 memset(a,0,sizeof(*a));a->parent=parent;a->profile=*profile;a->phase=BUILDING;
 int retained=i9100_peripheral_native_hold_get(parent,&a->parent_hold);if(retained){dispose(a);return retained;}
 a->lifecycle=IOLockAlloc();a->pmu_lock=IOLockAlloc();a->audss_lock=IOLockAlloc();
 if(!a->lifecycle||!a->pmu_lock||!a->audss_lock){dispose(a);return I9100_AP_IO;}
 const uint32_t addresses[MAPS]={0x10020000U,0x10000000U,0x03810000U,0x03830000U,0x12680000U};
 for(unsigned n=0;n<MAPS;n++)if(!map(a,n,addresses[n],n==PMU?16384U:4096U)){dispose(a);return I9100_AP_IO;}
 i9100_audio_pmu_io pi={a,pmu_read,pmu_write,pmu_take,pmu_drop,now,delay,lease_valid,owners_idle,transition_get,transition_put,confirm_bus};
 i9100_audss_io ai={a,aud_read,aud_write,aud_take,aud_drop,domain_get,domain_put,ap_idle};
 int e=lease_valid(a);if(e){dispose(a);return I9100_AP_OWNER;}
 e=i9100_audio_mclk_init(&a->mclk,&pi,24000000);if(!e)e=i9100_audio_domain_init(&a->domain,&pi);
 if(!e)e=i9100_audss_init(&a->audss,&ai,&profile->verified_rates);
 if(e){dispose(a);return e;}
 /* Establish silence before changing MCLK, including the inherited state. */
 a->stage=1;e=i9100_peripheral_native_i2c_open_owned(parent,&a->bus_lease,1,profile->verified_i2c6_pclk_hz,&a->bus);
 a->bus_owned=a->bus_lease.owned;
 if(!e)e=i9100_mc1n2_init(&a->codec,&a->bus);
 if(!e)e=i9100_mc1n2_enable(&a->codec,1);
 if(!e&&a->codec.id!=0x78U&&a->codec.id!=0x79U)e=I9100_MC_ID;
 /* Own codec state is muted; PMU acquisition may disregard its enabled flag
  * during startup but must still prove AP/DMA quiescence. */
 if(!e){a->mclk.io.consumers_idle=ap_idle;a->domain.io.consumers_idle=ap_idle;}
 if(!e)a->stage=2;if(!e)e=i9100_audio_mclk_enable(&a->mclk,1);if(!e)e=i9100_audio_mclk_acquire(&a->mclk);
 if(!e)a->stage=3;if(!e)e=i9100_audio_domain_enable(&a->domain,1);if(!e)e=i9100_audss_enable(&a->audss,1);
 if(!e)e=i9100_audss_acquire(&a->audss,profile->use_config?&profile->audss_config:0);
 if(!e)a->stage=4;if(!e)e=i9100_gpio_claim(gpio,I9100_GPZ,0x7fU,&a->pins);
 for(unsigned n=0;n<7U&&!e;n++)e=i9100_gpio_input(&a->pins,n);
 if(!e)a->stage=5;if(!e)e=i9100_mc1n2_cold_init(&a->codec,24000000,profile->sample_rate,1);
 i9100_mc1n2_clock_guard guard={a,clock_lease,ap_idle};
 if(!e)a->stage=6;if(!e)e=i9100_mc1n2_clock_start(&a->codec,&guard);
 a->mclk.io.consumers_idle=owners_idle;a->domain.io.consumers_idle=owners_idle;
 if(e){a->error=e;int off=cleanup(a);if(off)*out=a;else dispose(a);return e;}
 a->phase=LIVE;*out=a;return 0;
}
enum {PCM_OPEN=1,PCM_CLOSE,PCM_QUEUE,PCM_START,PCM_SERVICE,OUTPUT_PREPARE,OUTPUT_ACTIVATE,OUTPUT_STOP,OUTPUT_MUTE,PCM_RELEASE};
static int authorized(i9100_audio_board_native *a,i9100_audio_board_worker_lease *l)
{
 if(!l)return a->worker_owner?I9100_AP_BUSY:0;
 return !l->owned||l->self!=(uintptr_t)l||l->board!=a||a->worker_owner!=l||
  l->generation!=a->worker_generation?I9100_AP_OWNER:0;
}
static int request(i9100_audio_board_native *a,i9100_audio_board_worker_lease *l,unsigned operation,
 const int16_t *samples=0,uint32_t value=0,int32_t gain=0,uint32_t *completed=0)
{
 if(completed)*completed=0;if(!a)return I9100_AP_ARGUMENT;if(take(a->lifecycle))return I9100_AP_BUSY;
 int e=authorized(a,l);if(e){IOLockUnlock(a->lifecycle);return e;}
 switch(operation){
 case PCM_OPEN:
  e=a->phase!=LIVE?I9100_AP_DISABLED:(a->pcm||a->gated)?I9100_AP_BUSY:
   a->profile.use_gated_dma?i9100_audio_gated_open(1,a->parent,&a->audss,&a->codec,&a->pins,a->profile.sample_rate,a->profile.period_frames,value,&a->gated):
   i9100_audio_native_open_with_pins(1,a->parent,&a->audss,&a->codec,&a->pins,a->profile.sample_rate,a->profile.period_frames,value,1,&a->pcm);
  if(e&&e!=I9100_AP_BUSY&&e!=I9100_AP_DISABLED){a->error=e;a->phase=CLOSING;}break;
 case PCM_CLOSE:
  if(a->gated&&!i9100_audio_gated_status(a->gated,&a->last_pcm))a->last_pcm_valid=1;
  if(a->pcm&&!i9100_audio_native_status(a->pcm,&a->last_pcm))a->last_pcm_valid=1;
  e=a->gated?i9100_audio_gated_close(&a->gated):a->pcm?i9100_audio_native_close(&a->pcm):0;
  if(!e&&a->output.owned)e=i9100_mc1n2_output_stop(&a->output);
  if(e)a->error=e;
  if(!e&&a->last_pcm_valid){a->last_pcm.prepared=a->last_pcm.started=a->last_pcm.held_mask=a->last_pcm.ready_mask=0;a->last_pcm.in_flight=2;}
  break;
 case PCM_QUEUE:
  e=a->phase!=LIVE?I9100_AP_DISABLED:a->gated?i9100_audio_gated_queue(a->gated,samples,value):a->pcm?i9100_audio_native_queue(a->pcm,samples,value):I9100_AP_DISABLED;break;
 case PCM_START:
  e=a->phase!=LIVE?I9100_AP_DISABLED:a->gated?i9100_audio_gated_start(a->gated):a->pcm?i9100_audio_native_start(a->pcm):I9100_AP_DISABLED;
  if(e&&e!=I9100_AP_DISABLED&&e!=I9100_PCM_BUSY){a->error=e;a->phase=CLOSING;}break;
 case PCM_SERVICE:
  e=!completed?I9100_AP_ARGUMENT:a->phase!=LIVE?I9100_AP_DISABLED:a->gated?i9100_audio_gated_service(a->gated,completed):a->pcm?i9100_audio_native_service(a->pcm,completed):I9100_AP_DISABLED;
  if(a->gated&&!i9100_audio_gated_status(a->gated,&a->last_pcm))a->last_pcm_valid=1;
  if(a->pcm&&!i9100_audio_native_status(a->pcm,&a->last_pcm))a->last_pcm_valid=1;
  if(e&&e!=I9100_PCM_BUSY&&e!=I9100_AP_DISABLED){a->error=e;a->phase=CLOSING;}break;
 case PCM_RELEASE:
  e=a->phase!=LIVE?I9100_AP_DISABLED:a->gated?i9100_audio_gated_release(a->gated):0;
  if(e&&e!=I9100_PCM_BUSY&&e!=I9100_AP_DISABLED){a->error=e;a->phase=CLOSING;}break;
 case OUTPUT_PREPARE:{
  i9100_mc1n2_output_guard g={a,clock_lease,output_idle,output_stream};
  e=a->phase==LIVE?i9100_mc1n2_output_prepare(1,&a->codec,(enum i9100_mc1n2_output)value,gain,&g,&a->output):I9100_MC_DISABLED;
  if(e)a->error=e;if(a->codec.fault)a->phase=CLOSING;break;}
 case OUTPUT_ACTIVATE:
  e=a->phase==LIVE?i9100_mc1n2_output_activate(&a->output):I9100_MC_DISABLED;
  if(e)a->error=e;if(a->codec.fault)a->phase=CLOSING;break;
 case OUTPUT_STOP:e=a->output.owned?i9100_mc1n2_output_stop(&a->output):0;if(e)a->error=e;break;
 case OUTPUT_MUTE:e=a->codec.enabled?i9100_mc1n2_mute(&a->codec):I9100_MC_DISABLED;if(e)a->error=e;break;
 default:e=I9100_AP_ARGUMENT;break;
 }
 IOLockUnlock(a->lifecycle);return e;
}
static int worker_request(i9100_audio_board_worker_lease *l,unsigned op,const int16_t *samples=0,
 uint32_t value=0,int32_t gain=0,uint32_t *completed=0)
{
 if(completed)*completed=0;
 if(!l||!l->owned||l->self!=(uintptr_t)l||!l->board)return I9100_AP_OWNER;
 return request(l->board,l,op,samples,value,gain,completed);
}
int i9100_audio_board_pcm_open(i9100_audio_board_native *a,uint32_t bclk){return request(a,0,PCM_OPEN,0,bclk);}
int i9100_audio_board_queue(i9100_audio_board_native *a,const int16_t *samples,uint32_t frames){return request(a,0,PCM_QUEUE,samples,frames);}
int i9100_audio_board_pcm_close(i9100_audio_board_native *a){return request(a,0,PCM_CLOSE);}
int i9100_audio_board_output_prepare(int enabled,i9100_audio_board_native *a,enum i9100_mc1n2_output kind,int32_t gain)
{return enabled!=1?I9100_MC_DISABLED:request(a,0,OUTPUT_PREPARE,0,kind,gain);}
int i9100_audio_board_output_activate(i9100_audio_board_native *a){return request(a,0,OUTPUT_ACTIVATE);}
int i9100_audio_board_output_stop(i9100_audio_board_native *a){return request(a,0,OUTPUT_STOP);}
int i9100_audio_board_output_mute(i9100_audio_board_native *a){return request(a,0,OUTPUT_MUTE);}
int i9100_audio_board_hold(i9100_audio_board_native *a)
{
 if(!a)return I9100_AP_ARGUMENT;if(take(a->lifecycle))return I9100_AP_BUSY;
 int e=a->phase==LIVE&&!a->worker_owner&&a->worker_holds!=0xffffffffU?0:I9100_AP_BUSY;if(!e)a->worker_holds++;
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_board_unhold(i9100_audio_board_native *a)
{
 if(!a)return I9100_AP_ARGUMENT;if(take(a->lifecycle))return I9100_AP_BUSY;
 int e=a->worker_owner||!a->worker_holds?I9100_AP_OWNER:0;if(!e)a->worker_holds--;
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_board_start(i9100_audio_board_native *a)
{return request(a,0,PCM_START);}
int i9100_audio_board_service(i9100_audio_board_native *a,uint32_t *completed)
{return !completed?I9100_AP_ARGUMENT:request(a,0,PCM_SERVICE,0,0,0,completed);}
int i9100_audio_board_worker_claim(i9100_audio_board_native *a,i9100_audio_board_worker_lease *l)
{
 if(!a||!l||l->owned)return I9100_AP_ARGUMENT;if(take(a->lifecycle))return I9100_AP_BUSY;
 int e=a->phase!=LIVE||a->pcm||a->gated||a->output.owned||a->worker_holds||a->worker_owner?I9100_AP_BUSY:0;
 if(!e&&a->worker_generation==0xffffffffU)e=I9100_AP_OWNER;
 if(!e){memset(l,0,sizeof(*l));l->self=(uintptr_t)l;l->board=a;l->generation=++a->worker_generation;l->owned=1;
  a->worker_owner=l;a->worker_holds=1;}
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_board_worker_release(i9100_audio_board_worker_lease *l)
{
 if(!l||!l->owned||l->self!=(uintptr_t)l||!l->board)return I9100_AP_OWNER;
 auto *a=l->board;if(take(a->lifecycle))return I9100_AP_BUSY;int e=authorized(a,l);
 if(!e&&(a->pcm||a->gated||a->output.owned))e=I9100_AP_BUSY;
 if(!e&&a->worker_holds!=1U)e=I9100_AP_OWNER;
 if(!e){l->owned=0;a->worker_owner=0;a->worker_holds=0;}
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_board_worker_pcm_open(i9100_audio_board_worker_lease *l,uint32_t bclk){return worker_request(l,PCM_OPEN,0,bclk);}
int i9100_audio_board_worker_pcm_close(i9100_audio_board_worker_lease *l){return worker_request(l,PCM_CLOSE);}
int i9100_audio_board_worker_queue(i9100_audio_board_worker_lease *l,const int16_t *samples,uint32_t frames){return worker_request(l,PCM_QUEUE,samples,frames);}
int i9100_audio_board_worker_start(i9100_audio_board_worker_lease *l){return worker_request(l,PCM_START);}
int i9100_audio_board_worker_service(i9100_audio_board_worker_lease *l,uint32_t *completed)
{return !completed?I9100_AP_ARGUMENT:worker_request(l,PCM_SERVICE,0,0,0,completed);}
int i9100_audio_board_worker_release_period(i9100_audio_board_worker_lease *l){return worker_request(l,PCM_RELEASE);}
int i9100_audio_board_gated_enabled(i9100_audio_board_native *a,uint32_t *out)
{if(!a||!out)return I9100_AP_ARGUMENT;if(take(a->lifecycle))return I9100_AP_BUSY;*out=a->profile.use_gated_dma;IOLockUnlock(a->lifecycle);return 0;}
int i9100_audio_board_worker_output_prepare(int enabled,i9100_audio_board_worker_lease *l,enum i9100_mc1n2_output kind,int32_t gain)
{return enabled!=1?I9100_MC_DISABLED:worker_request(l,OUTPUT_PREPARE,0,kind,gain);}
int i9100_audio_board_worker_output_activate(i9100_audio_board_worker_lease *l){return worker_request(l,OUTPUT_ACTIVATE);}
int i9100_audio_board_worker_output_mute(i9100_audio_board_worker_lease *l){return worker_request(l,OUTPUT_MUTE);}
int i9100_audio_board_status(i9100_audio_board_native *a,struct i9100_audio_board_status *out,i9100_pcm_status *pcm)
{
 if(!a||!out)return I9100_AP_ARGUMENT;if(take(a->lifecycle))return I9100_AP_BUSY;
 struct i9100_audio_board_status value;memset(&value,0,sizeof(value));value.phase=a->phase;value.stage=a->stage;
 value.mclk_owned=a->mclk.owned;value.domain_owned=a->domain.owned;value.audss_active=a->audss.active;
 value.pins_owned=a->pins.active;value.bus_owned=a->bus_owned;value.codec_enabled=a->codec.enabled;
 value.cold_owned=a->codec.cold_owned;value.clock_owned=a->codec.clock_owned;value.pcm_owned=a->pcm!=0||a->gated!=0;value.error=a->error;*out=value;
 out->output_owned=a->output.owned;out->output_powered=a->codec.output_powered;out->output_running=a->codec.output_running;
 out->output_stage=a->codec.output_stage;out->output_error=a->codec.output_error;
 out->codec_fault=a->codec.fault;
 out->output_cleanup_error=a->codec.output_cleanup_error;out->output_gain_mdb=a->codec.actual_gain_mdb;
 out->worker_holds=a->worker_holds;
 int e=0;if(pcm){memset(pcm,0,sizeof(*pcm));if(a->gated)e=i9100_audio_gated_status(a->gated,pcm);
  else if(a->pcm)e=i9100_audio_native_status(a->pcm,pcm);
  else if(a->last_pcm_valid)*pcm=a->last_pcm;}
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_board_close(i9100_audio_board_native **owner)
{
 if(!owner||!*owner)return I9100_AP_ARGUMENT;i9100_audio_board_native *a=*owner;
 if(take(a->lifecycle))return I9100_AP_BUSY;if(a->worker_holds){IOLockUnlock(a->lifecycle);return I9100_AP_BUSY;}
 int e=cleanup(a);if(e)a->error=e;IOLockUnlock(a->lifecycle);
 if(!e){dispose(a);*owner=0;}return e;
}
