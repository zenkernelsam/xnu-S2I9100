/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_native.h"
#include "i9100_dma_native.h"
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
enum {PDMA_MAP,I2S_MAP,GIC_MAP,MAPS,BUILDING=1,READY=2,CLOSING=3};
struct i9100_audio_native {
 i9100_peripheral_native *parent;i9100_audss *audss;i9100_mc1n2 *codec;
 IOMemoryDescriptor *memory[MAPS];IOMemoryMap *mapping[MAPS];volatile uint8_t *regs[MAPS];
 IOLock *lifecycle,*stream_lock,*i2s_lock,*engine_lock;
 i9100_clock_lease gate;i9100_gpio_lease pins;
 i9100_pl330 dma;i9100_i2s i2s;i9100_pcm_stream pcm;
 i9100_dma_native *allocation[3];unsigned phase;
};
static int take(IOLock *lock)
{return ml_get_interrupts_enabled()&&IOLockTryLock(lock)?0:-1;}
static uint32_t read(i9100_audio_native *a,unsigned map,unsigned offset)
{OSSynchronizeIO();uint32_t value=*(volatile uint32_t *)(a->regs[map]+offset);OSSynchronizeIO();return value;}
static void write(i9100_audio_native *a,unsigned map,unsigned offset,uint32_t value)
{OSSynchronizeIO();*(volatile uint32_t *)(a->regs[map]+offset)=value;OSSynchronizeIO();}
static uint32_t dma_read(void *c,unsigned off){return read((i9100_audio_native *)c,PDMA_MAP,off);}
static void dma_write(void *c,unsigned off,uint32_t v){write((i9100_audio_native *)c,PDMA_MAP,off,v);}
static uint32_t i2s_read(void *c,unsigned off){return read((i9100_audio_native *)c,I2S_MAP,off);}
static void i2s_write(void *c,unsigned off,uint32_t v){write((i9100_audio_native *)c,I2S_MAP,off,v);}
static int dma_take(void *c){return take(((i9100_audio_native *)c)->engine_lock);}
static void dma_drop(void *c){IOLockUnlock(((i9100_audio_native *)c)->engine_lock);}
static int i2s_take(void *c){return take(((i9100_audio_native *)c)->i2s_lock);}
static void i2s_drop(void *c){IOLockUnlock(((i9100_audio_native *)c)->i2s_lock);}
static int stream_take(void *c){return take(((i9100_audio_native *)c)->stream_lock);}
static void stream_drop(void *c){IOLockUnlock(((i9100_audio_native *)c)->stream_lock);}
static uint64_t now(void *){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
static int delay(void *,uint32_t us){if(!ml_get_interrupts_enabled()||us>100000U)return -1;IODelay(us);return 0;}
static int dma_idle(void *c){return i9100_pl330_idle(&((i9100_audio_native *)c)->dma);}
static int dma_stop(void *c){return i9100_pl330_stop(&((i9100_audio_native *)c)->dma);}
static int muted(void *c){return i9100_mc1n2_mute(((i9100_audio_native *)c)->codec);}
static int controller_idle(i9100_audio_native *a)
{
 if(read(a,PDMA_MAP,0xd00U)&1U||read(a,PDMA_MAP,0x30U)||read(a,PDMA_MAP,0x34U))return I9100_PCM_BUSY;
 for(unsigned n=0;n<8U;n++)if(read(a,PDMA_MAP,0x100U+n*8U)&15U)return I9100_PCM_BUSY;
 if(read(a,PDMA_MAP,0x20U)&~(a->dma.enabled?(1U<<3):0U))return I9100_PCM_BUSY;
 return 0;
}
static bool map(i9100_audio_native *a,unsigned index,uint32_t pa)
{
 a->memory[index]=IOMemoryDescriptor::withPhysicalAddress(pa,4096,kIODirectionInOut);
 a->mapping[index]=a->memory[index]?a->memory[index]->map(kIOMapInhibitCache):0;
 a->regs[index]=a->mapping[index]?(volatile uint8_t *)a->mapping[index]->getAddress():0;
 return a->regs[index]!=0;
}
static void dispose(i9100_audio_native *a)
{
 for(unsigned n=0;n<MAPS;n++){if(a->mapping[n])a->mapping[n]->release();if(a->memory[n])a->memory[n]->release();}
 if(a->stream_lock)IOLockFree(a->stream_lock);if(a->i2s_lock)IOLockFree(a->i2s_lock);
 if(a->engine_lock)IOLockFree(a->engine_lock);if(a->lifecycle)IOLockFree(a->lifecycle);
 IOFree(a,sizeof(*a));
}
static int cleanup(i9100_audio_native *a)
{
 a->phase=CLOSING;int e;
 if(a->pcm.initialized&&(a->pcm.prepared||a->pcm.started||a->pcm.held_mask))e=i9100_pcm_stream_stop(&a->pcm);
 else{e=muted(a);if(!e&&a->pcm.initialized)e=i9100_pcm_stream_stop(&a->pcm);}
 if(e)return e;
 if(a->gate.active){
  if(controller_idle(a))return I9100_PCM_BUSY;
  if(a->dma.initialized&&(a->dma.enabled||a->dma.active||a->dma.fault)){e=i9100_pl330_stop(&a->dma);if(e)return e;}
 }
 if(a->pcm.initialized&&a->pcm.enabled){e=i9100_pcm_stream_enable(&a->pcm,0);if(e)return e;}
 if(a->i2s.initialized&&a->i2s.enabled){e=i9100_i2s_enable(&a->i2s,0);if(e)return e;}
 if(a->dma.initialized&&a->dma.enabled){e=i9100_pl330_enable(&a->dma,0);if(e)return e;}
 for(unsigned n=0;n<3;n++)if(a->allocation[n]){
  i9100_dma_buffer *b=i9100_dma_native_buffer(a->allocation[n]);
  /* A previous release failure kept this allocation quarantined. Only a
   * later explicit close, after verified engine shutdown, may reclaim it. */
  if(b&&b->state==I9100_DMA_QUARANTINED){e=i9100_dma_abort(b);if(e)return e;}
  e=i9100_dma_native_destroy(&a->allocation[n]);if(e)return e;
 }
 if(a->pins.active){e=i9100_gpio_release(&a->pins,I9100_GPIO_RESTORE);if(e)return e;}
 if(a->gate.active){e=i9100_clock_release(&a->gate);if(e)return e;}
 return 0;
}
int i9100_audio_native_open(int enabled,i9100_peripheral_native *parent,i9100_audss *audss,
 i9100_mc1n2 *codec,uint32_t rate,uint32_t frames,uint32_t bclk,int verified,i9100_audio_native **out)
{
 if(!out)return I9100_PCM_ARGUMENT;if(*out)return I9100_PCM_BUSY;if(enabled!=1)return I9100_PCM_DISABLED;
 if(!ml_get_interrupts_enabled()||!parent||!audss||!codec||verified!=1||
    (rate!=44100U&&rate!=48000U)||frames<16U||frames>4096U||bclk!=rate*32U||
    !audss->active||!audss->domain_owned||audss->fault||!audss->rates.bus||!audss->rates.i2s||
    !codec->enabled||!codec->cold_owned||codec->cold_stage!=5U||codec->id!=0x79U||
    codec->mclk_hz!=24000000U||codec->sample_rate!=rate)return I9100_PCM_ARGUMENT;
 i9100_gpio_broker *gpio=i9100_peripheral_native_gpio(parent);
 i9100_clock_gates *clocks=i9100_peripheral_native_clocks(parent);
 if(!gpio||!clocks||!gpio->enabled||!clocks->enabled||gpio->count!=I9100_BANK_COUNT||clocks->count!=I9100_GATE_COUNT)
  return I9100_PCM_DISABLED;
 i9100_audio_native *a=(i9100_audio_native *)IOMalloc(sizeof(*a));if(!a)return I9100_PCM_IO;
 memset(a,0,sizeof(*a));a->parent=parent;a->audss=audss;a->codec=codec;a->phase=BUILDING;
 a->lifecycle=IOLockAlloc();a->stream_lock=IOLockAlloc();a->i2s_lock=IOLockAlloc();a->engine_lock=IOLockAlloc();
 if(!a->lifecycle||!a->stream_lock||!a->i2s_lock||!a->engine_lock){dispose(a);return I9100_PCM_IO;}
 const uint32_t addresses[MAPS]={0x12680000U,0x03830000U,0x10490000U};
 for(unsigned n=0;n<MAPS;n++)if(!map(a,n,addresses[n])){dispose(a);return I9100_PCM_IO;}
 i9100_pl330_io di={a,dma_read,dma_write,dma_take,dma_drop,now,delay};
 i9100_i2s_io ii={a,i2s_read,i2s_write,i2s_take,i2s_drop,dma_idle,dma_stop};
 i9100_pcm_io pi={a,stream_take,stream_drop,muted};
 uint8_t pads=0,start=0;int e=i9100_mc1n2_read(codec,I9100_MC_BASE,12,&pads);
 if(!e)e=i9100_mc1n2_read(codec,I9100_MC_MIXER,137,&start);
 /* Cold init deliberately masks these pads; do not confuse that with clocks. */
 if(!e&&((pads&0xf0U)!=0x50U||(start&0x1cU)!=0x1cU))e=I9100_MC_NOT_READY;
 if(!e&&(read(a,GIC_MAP,0x108U)&(1U<<3)||read(a,GIC_MAP,0x110U)&(1U<<1)))e=I9100_PCM_BUSY;
 if(e){dispose(a);return e;}
 e=muted(a);if(e){*out=a;return e;}
 e=i9100_pl330_init(&a->dma,&di,0,3);if(!e)e=i9100_i2s_init(&a->i2s,&ii);
 if(!e)e=i9100_clock_acquire(clocks,I9100_GATE_PDMA0,&a->gate);
 if(!e&&(clocks->gates[I9100_GATE_PDMA0].refs!=1U||controller_idle(a)))e=I9100_PCM_BUSY;
 if(!e)e=i9100_gpio_claim(gpio,I9100_GPZ,0x7fU,&a->pins);
 for(unsigned n=0;n<7U&&!e;n++)e=i9100_gpio_function(&a->pins,n,2);
 if(!e)e=i9100_pl330_enable(&a->dma,1);
 if(!e)e=i9100_i2s_enable(&a->i2s,1);
 unsigned bytes=((frames*4U+4095U)/4096U)*4096U;
 for(unsigned n=0;n<3U&&!e;n++){
  e=i9100_dma_native_allocate(1,n==2U?4096U:bytes,&a->dma,i9100_pl330_idle,&a->allocation[n]);
  if(!e)e=i9100_dma_enable(i9100_dma_native_buffer(a->allocation[n]),1);
 }
 if(!e)e=i9100_pcm_stream_init(&a->pcm,&pi,&a->dma,&a->i2s,
  i9100_dma_native_buffer(a->allocation[0]),i9100_dma_native_buffer(a->allocation[1]),i9100_dma_native_buffer(a->allocation[2]));
 if(!e)e=i9100_pcm_stream_enable(&a->pcm,1);
 if(!e)e=i9100_pcm_stream_prepare(&a->pcm,rate,frames,bclk,verified);
 if(e){int off=cleanup(a);if(off)*out=a;else dispose(a);return e;}
 a->phase=READY;*out=a;return 0;
}
int i9100_audio_native_queue(i9100_audio_native *a,const int16_t *samples,uint32_t frames)
{
 if(!a)return I9100_PCM_ARGUMENT;if(take(a->lifecycle))return I9100_PCM_BUSY;
 int e=a->phase==READY?i9100_pcm_stream_queue(&a->pcm,samples,frames):I9100_PCM_DISABLED;
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_native_start(i9100_audio_native *a)
{
 if(!a)return I9100_PCM_ARGUMENT;if(take(a->lifecycle))return I9100_PCM_BUSY;
 int e=a->phase==READY?i9100_pcm_stream_start(&a->pcm):I9100_PCM_DISABLED;
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_native_service(i9100_audio_native *a,uint32_t *completed)
{
 if(completed)*completed=0;if(!a||!completed)return I9100_PCM_ARGUMENT;if(take(a->lifecycle))return I9100_PCM_BUSY;
 int e=a->phase==READY?i9100_pcm_stream_service(&a->pcm,completed):I9100_PCM_DISABLED;
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_native_status(i9100_audio_native *a,i9100_pcm_status *out)
{
 if(!a||!out)return I9100_PCM_ARGUMENT;if(take(a->lifecycle))return I9100_PCM_BUSY;
 int e=a->pcm.initialized?i9100_pcm_stream_status(&a->pcm,out):I9100_PCM_DISABLED;
 IOLockUnlock(a->lifecycle);return e;
}
int i9100_audio_native_close(i9100_audio_native **owner)
{
 if(!owner||!*owner)return I9100_PCM_ARGUMENT;i9100_audio_native *a=*owner;
 if(take(a->lifecycle))return I9100_PCM_BUSY;
 int e=cleanup(a);IOLockUnlock(a->lifecycle);
 if(!e){dispose(a);*owner=0;}return e;
}
