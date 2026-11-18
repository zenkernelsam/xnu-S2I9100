/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_gated.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
uint32_t i9100_ring_mmio_read(volatile uint8_t *,unsigned);
void i9100_ring_mmio_write(volatile uint8_t *,unsigned,uint32_t);
}
enum {GA_BUILDING=1,GA_READY=2,GA_CLOSING=3};
struct i9100_audio_gated {
 i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;
 i9100_audss *audss;i9100_mc1n2 *codec;i9100_gpio_lease *pins;
 IOLock *lifecycle,*i2s_lock;IOMemoryDescriptor *memory;IOMemoryMap *mapping;volatile uint8_t *regs;
 i9100_i2s i2s;i9100_pl330_ring_native *ring;
 i9100_pcm_status pcm;uint32_t phase,queued;
};
static int take(IOLock *lock){return ml_get_interrupts_enabled()&&IOLockTryLock(lock)?0:-1;}
static uint32_t read_i2s(void *c,unsigned off){return i9100_ring_mmio_read(((i9100_audio_gated *)c)->regs,off);}
static void write_i2s(void *c,unsigned off,uint32_t value){i9100_ring_mmio_write(((i9100_audio_gated *)c)->regs,off,value);}
static int i2s_take(void *c){return take(((i9100_audio_gated *)c)->i2s_lock);}
static void i2s_drop(void *c){IOLockUnlock(((i9100_audio_gated *)c)->i2s_lock);}
static uint64_t now_ns(){uint64_t value=0;absolutetime_to_nanoseconds(mach_absolute_time(),&value);return value;}
static int resources(void *c)
{
 i9100_audio_gated *g=(i9100_audio_gated *)c;
 if(!g->parent_hold.owned||!g->audss->active||g->audss->fault||!g->audss->domain_owned||
    !g->codec->enabled||!g->codec->cold_owned||g->codec->cold_stage!=5U||
    !g->codec->clock_owned||!g->codec->clock_active||g->codec->sample_rate!=g->pcm.sample_rate||
    g->codec->mclk_hz!=24000000U||!g->codec->clock_guard.lease_valid||
    g->codec->clock_guard.lease_valid(g->codec->clock_guard.cookie))return -1;
 i9100_gpio_broker *gpio=i9100_peripheral_native_gpio(g->parent);
 if(!gpio||!g->pins->active||g->pins->broker!=gpio||g->pins->bank!=I9100_GPZ||g->pins->mask!=0x7fU||
    gpio->io.lock(gpio->io.cookie))return -1;
 bool owned=true;for(unsigned n=0;n<7;n++)if(gpio->banks[I9100_GPZ].owners[n]!=g->pins->generation)owned=false;
 gpio->io.unlock(gpio->io.cookie);return owned?0:-1;
}
static int mute(void *c){return i9100_mc1n2_mute(((i9100_audio_gated *)c)->codec);}
static int start_requests(void *c){return i9100_i2s_start(&((i9100_audio_gated *)c)->i2s,0,1);}
static int quiesce_requests(void *c){return i9100_i2s_quiesce(&((i9100_audio_gated *)c)->i2s);}
static int dma_idle(void *c)
{return ((i9100_audio_gated *)c)->ring?-1:0;}
static int dma_stop(void *c)
{return ((i9100_audio_gated *)c)->ring?I9100_I2S_BUSY:0;}
static int pads_idle(void *c)
{
 i9100_audio_gated *g=(i9100_audio_gated *)c;
 for(unsigned n=0;n<7;n++){int e=i9100_gpio_input(g->pins,n);if(e)return e;}return 0;
}
static int mapped_error(int e)
{
 if(!e)return 0;if(e>=I9100_PCM_OVERFLOW&&e<=I9100_PCM_ARGUMENT)return e;
 if(e==I9100_PDMA_BUSY)return I9100_PCM_BUSY;
 if(e==I9100_PDMA_ARGUMENT)return I9100_PCM_ARGUMENT;if(e==I9100_PDMA_DISABLED)return I9100_PCM_DISABLED;
 return e==I9100_PDMA_OWNER?I9100_PCM_OWNER:I9100_PCM_IO;
}
static void capture(i9100_audio_gated *g)
{
 if(!g->ring)return;struct i9100_pl330_ring_status s={};
 if(i9100_pl330_ring_native_status(g->ring,&s))return;
 g->pcm.completed_frames=s.completed_frames;g->pcm.completed_periods=s.completed_periods;
 g->pcm.completed_silence_periods=s.completed_silence_periods;g->pcm.held_mask=s.held_mask;
 g->pcm.in_flight=s.pending?s.pending_slot:s.expected_slot;
 if(s.fault){g->pcm.fault=1;if(!g->pcm.last_error)g->pcm.last_error=s.error;}
 if(s.cleanup_error)g->pcm.cleanup_error=s.cleanup_error;
}
static int cleanup(i9100_audio_gated *g)
{
 g->phase=GA_CLOSING;capture(g);
 if(g->ring){int e=i9100_pl330_ring_native_close(&g->ring);if(e){g->pcm.cleanup_error=e;return e;}}
 if(g->i2s.prepared){int e=i9100_i2s_stop(&g->i2s);if(e){g->pcm.cleanup_error=e;return e;}}
 if(g->i2s.enabled){int e=i9100_i2s_enable(&g->i2s,0);if(e){g->pcm.cleanup_error=e;return e;}}
 if(g->parent_hold.owned){int e=i9100_peripheral_native_hold_put(&g->parent_hold);if(e){g->pcm.cleanup_error=e;return e;}}
 g->pcm.prepared=g->pcm.started=g->pcm.held_mask=g->pcm.ready_mask=0;g->pcm.in_flight=2;return 0;
}
static void dispose(i9100_audio_gated *g)
{if(g->mapping)g->mapping->release();if(g->memory)g->memory->release();if(g->i2s_lock)IOLockFree(g->i2s_lock);if(g->lifecycle)IOLockFree(g->lifecycle);IOFree(g,sizeof(*g));}
int i9100_audio_gated_open(int enabled,i9100_peripheral_native *parent,i9100_audss *audss,i9100_mc1n2 *codec,
 i9100_gpio_lease *pins,uint32_t rate,uint32_t frames,uint32_t bclk,i9100_audio_gated **out)
{
 if(!out||*out)return I9100_PCM_ARGUMENT;if(enabled!=1)return I9100_PCM_DISABLED;
 if(!parent||!audss||!codec||!pins||(rate!=44100U&&rate!=48000U)||frames<16U||frames>512U||
    bclk!=rate*32U||!ml_get_interrupts_enabled())return I9100_PCM_ARGUMENT;
 i9100_audio_gated *g=(i9100_audio_gated *)IOMalloc(sizeof(*g));if(!g)return I9100_PCM_IO;
 memset(g,0,sizeof(*g));g->parent=parent;g->audss=audss;g->codec=codec;g->pins=pins;g->phase=GA_BUILDING;
 g->pcm.sample_rate=rate;g->pcm.period_frames=frames;g->pcm.in_flight=2;
 int e=i9100_peripheral_native_hold_get(parent,&g->parent_hold);if(e){dispose(g);return e;}
 g->lifecycle=IOLockAlloc();g->i2s_lock=IOLockAlloc();
 if(!g->lifecycle||!g->i2s_lock){e=I9100_PCM_IO;goto failed;}
 if(resources(g)){e=I9100_PCM_OWNER;goto failed;}
 if(codec->fault){e=I9100_PCM_OWNER;goto failed;}
 g->memory=IOMemoryDescriptor::withPhysicalAddress(0x03830000U,4096,kIODirectionInOut);
 g->mapping=g->memory?g->memory->map(kIOMapInhibitCache):0;
 g->regs=g->mapping?(volatile uint8_t *)g->mapping->getAddress():0;
 if(!g->regs){e=I9100_PCM_IO;goto failed;}
 {
  i9100_gpio_broker *gpio=i9100_peripheral_native_gpio(parent);
  if(gpio->io.lock(gpio->io.cookie)){e=I9100_PCM_BUSY;goto failed;}
  uint32_t con=gpio->io.read32(gpio->io.cookie,2,0);gpio->io.unlock(gpio->io.cookie);
  uint32_t mode=read_i2s(g,4);
  for(unsigned n=0;n<3;n++){unsigned function=(con>>(n*4U))&15U;
   if(function&&!(function==2U&&(mode&0x1800U)==0x1800U)){e=I9100_PCM_OWNER;goto failed;}}
 }
 {
  uint8_t pads=0,start=0,fs=0;e=i9100_mc1n2_read(codec,I9100_MC_BASE,12,&pads);
  if(!e)e=i9100_mc1n2_read(codec,I9100_MC_MIXER,137,&start);
  if(!e)e=i9100_mc1n2_read(codec,I9100_MC_MIXER,135,&fs);
  if(!e&&((pads&0xf0U)!=0x50U||(start&0x4cU)!=0x4cU||(fs&0x7fU)!=(rate==48000U?0x20U:0x21U)))e=I9100_PCM_OWNER;
  if(e)goto failed;
 }
 e=mute(g);if(e)goto failed;
 {
  i9100_i2s_io io={g,read_i2s,write_i2s,i2s_take,i2s_drop,dma_idle,dma_stop,pads_idle};
  e=i9100_i2s_init(&g->i2s,&io);if(!e)e=i9100_i2s_enable(&g->i2s,1);
  if(!e)e=i9100_i2s_configure(&g->i2s,rate,bclk,1);if(e)goto failed;
 }
 for(unsigned n=0;n<7;n++){e=i9100_gpio_function(pins,n,2);if(e)goto failed;}
 {
  i9100_ring_consumer consumer={g,resources,mute,start_requests,quiesce_requests};
  e=i9100_pl330_ring_native_open(1,parent,&consumer,rate,frames,&g->ring);if(e)goto failed;
 }
 g->pcm.enabled=g->pcm.prepared=1;g->pcm.held_mask=15;g->pcm.zero_periods=2;g->phase=GA_READY;*out=g;return 0;
failed:
 g->pcm.fault=1;g->pcm.last_error=e;
 {int off=cleanup(g);if(off){*out=g;return mapped_error(e);}dispose(g);return mapped_error(e);}
}
static int run(i9100_audio_gated *g,unsigned op,const int16_t *samples,uint32_t frames,uint32_t *complete)
{
 if(complete)*complete=0;if(!g||!g->lifecycle)return I9100_PCM_ARGUMENT;if(take(g->lifecycle))return I9100_PCM_BUSY;
 int e=0;
 if(g->phase!=GA_READY||g->pcm.fault){e=I9100_PCM_FAULT;goto done;}
 if(op!=1U&&!g->pcm.started){e=I9100_PCM_BUSY;goto done;}
 switch(op){
 case 1:
  if(g->pcm.started){e=I9100_PCM_BUSY;break;}
  e=i9100_pl330_ring_native_start(g->ring);if(!e){g->pcm.started=1;g->pcm.start_time_ns=now_ns();g->pcm.timestamp_valid=g->pcm.start_time_ns!=0;}break;
 case 2:{
  if(g->pcm.queued_frames>~(uint64_t)0-frames){e=I9100_PCM_OVERFLOW;break;}
  e=i9100_pl330_ring_native_queue(g->ring,samples,frames);if(!e){g->pcm.queued_frames+=frames;g->queued=1;}break;}
 case 3:
  e=i9100_pl330_ring_native_poll(g->ring,complete);
  if(!e&&*complete){struct i9100_pl330_ring_status s={};(void)i9100_pl330_ring_native_status(g->ring,&s);
   if(s.fence_ns<=g->pcm.start_time_ns||s.fence_ns<=g->pcm.completion_time_ns)g->pcm.timestamp_valid=0;
   g->pcm.completion_time_ns=s.fence_ns;}break;
 case 4:
  e=i9100_pl330_ring_native_release_period(g->ring);
  if(!e){if(!g->queued&&g->pcm.zero_periods!=~(uint64_t)0)g->pcm.zero_periods++;g->queued=0;}break;
 default:e=I9100_PCM_ARGUMENT;break;
 }
 if(e&&e!=I9100_PDMA_BUSY&&e!=I9100_PCM_BUSY&&e!=I9100_PDMA_ARGUMENT&&e!=I9100_PCM_ARGUMENT){
  g->pcm.fault=1;g->pcm.last_error=e;g->phase=GA_CLOSING;
 }
 e=mapped_error(e);
done:capture(g);IOLockUnlock(g->lifecycle);return e;
}
int i9100_audio_gated_start(i9100_audio_gated *g){return run(g,1,0,0,0);}
int i9100_audio_gated_queue(i9100_audio_gated *g,const int16_t *samples,uint32_t frames){return run(g,2,samples,frames,0);}
int i9100_audio_gated_service(i9100_audio_gated *g,uint32_t *out){if(!out)return I9100_PCM_ARGUMENT;return run(g,3,0,0,out);}
int i9100_audio_gated_release(i9100_audio_gated *g){return run(g,4,0,0,0);}
int i9100_audio_gated_status(i9100_audio_gated *g,i9100_pcm_status *out)
{if(!g||!out||!g->lifecycle)return I9100_PCM_ARGUMENT;if(take(g->lifecycle))return I9100_PCM_BUSY;capture(g);*out=g->pcm;IOLockUnlock(g->lifecycle);return 0;}
int i9100_audio_gated_output_ready(i9100_audio_gated *g,uint32_t rate)
{if(!g||!g->lifecycle)return I9100_PCM_ARGUMENT;if(take(g->lifecycle))return I9100_PCM_BUSY;
 int e=g->phase==GA_READY&&g->pcm.started&&g->ring?i9100_pl330_ring_native_output_ready(g->ring,rate):I9100_PDMA_BUSY;
 IOLockUnlock(g->lifecycle);return mapped_error(e);}
int i9100_audio_gated_consumers_idle(i9100_audio_gated *g)
{if(!g||!g->lifecycle)return I9100_PCM_ARGUMENT;if(take(g->lifecycle))return I9100_PCM_BUSY;
 int e=g->ring?i9100_pl330_ring_native_consumers_idle(g->ring):0;IOLockUnlock(g->lifecycle);return mapped_error(e);}
int i9100_audio_gated_close(i9100_audio_gated **owner)
{if(!owner||!*owner)return I9100_PCM_ARGUMENT;i9100_audio_gated *g=*owner;if(g->lifecycle&&take(g->lifecycle))return I9100_PCM_BUSY;
 int e=cleanup(g);if(g->lifecycle)IOLockUnlock(g->lifecycle);if(!e){dispose(g);*owner=0;}return mapped_error(e);}
