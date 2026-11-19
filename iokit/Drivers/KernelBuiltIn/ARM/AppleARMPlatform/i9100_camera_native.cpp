/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_camera_native.h"
#include "i9100_dma_native.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <string.h>
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffU
#endif
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
uint32_t i9100_camera_mmio_read(volatile uint8_t *,unsigned);
void i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
}
enum {CN_FIMC,CN_CSIS,CN_SYSMMU,CN_CMU,CN_PMU,CN_GIC,CN_MAPS,CN_BUILDING=1,CN_READY=2,CN_CLOSING=3};
static volatile uint32_t fimc0_reserved;
struct i9100_camera_native {
 i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;
 i9100_camera_supply supply;i9100_camera_supply_lease supply_lease;
 i9100_camera_native_config config;
 IOLock *lifecycle,*fimc_lock,*csis_lock;
 IOMemoryDescriptor *memory[CN_MAPS];IOMemoryMap *mapping[CN_MAPS];volatile uint8_t *regs[CN_MAPS];
 i9100_dma_native *allocation;i9100_fimc fimc;i9100_csis csis;
 uint32_t phase,reserved,cmu_source,cmu_divider;int error,cleanup_error;
};
static int take(IOLock *lock){return ml_get_interrupts_enabled()&&lock&&IOLockTryLock(lock)?0:-1;}
static uint32_t rd(i9100_camera_native *n,unsigned map,unsigned off)
{return map>=CN_MAPS||!n->regs[map]?UINT32_MAX:i9100_camera_mmio_read(n->regs[map],off);}
static void wr(i9100_camera_native *n,unsigned map,unsigned off,uint32_t v)
{if(map<CN_MAPS&&n->regs[map])i9100_camera_mmio_write(n->regs[map],off,v);}
static uint32_t f_read(void *v,unsigned off){return rd((i9100_camera_native *)v,CN_FIMC,off);}
static void f_write(void *v,unsigned off,uint32_t x){wr((i9100_camera_native *)v,CN_FIMC,off,x);}
static uint32_t c_read(void *v,unsigned off){return rd((i9100_camera_native *)v,CN_CSIS,off);}
static void c_write(void *v,unsigned off,uint32_t x){wr((i9100_camera_native *)v,CN_CSIS,off,x);}
static int f_lock(void *v){return take(((i9100_camera_native *)v)->fimc_lock);}
static void f_unlock(void *v){IOLockUnlock(((i9100_camera_native *)v)->fimc_lock);}
static int c_lock(void *v){return take(((i9100_camera_native *)v)->csis_lock);}
static void c_unlock(void *v){IOLockUnlock(((i9100_camera_native *)v)->csis_lock);}
static void barrier(void *){OSSynchronizeIO();}
static uint64_t ns(void *){uint64_t t=0;absolutetime_to_nanoseconds(mach_absolute_time(),&t);return t;}
static int delay(void *,uint32_t us)
{if(!ml_get_interrupts_enabled()||us>1000U)return -1;IODelay(us);return 0;}
static bool supply_valid(i9100_camera_native *n)
{
 return n->supply_lease.owned==1U&&n->supply_lease.self==(uintptr_t)&n->supply_lease&&
  n->supply_lease.provider&&(n->supply_lease.generation!=0U)&&
  n->supply_lease.csis_index==n->config.fimc.csis_index&&
  !n->supply.validate(n->supply.cookie,&n->supply_lease);
}
static int resources(i9100_camera_native *n)
{
 if(!supply_valid(n))return I9100_CE_OWNER;
 uint32_t gate=rd(n,CN_CMU,0xc920),src=rd(n,CN_CMU,0xc220),div=rd(n,CN_CMU,0xc520);
 uint32_t phy=rd(n,CN_PMU,0x710+4U*n->config.fimc.csis_index),mmu=rd(n,CN_SYSMMU,0);
 uint32_t masks=(1U<<0)|(1U<<7)|(1U<<(4U+n->config.fimc.csis_index));
 if(gate==UINT32_MAX||src==UINT32_MAX||div==UINT32_MAX||phy==UINT32_MAX||mmu==UINT32_MAX||
    (gate&masks)!=masks||(phy&3U)!=3U||mmu)return I9100_CE_OWNER;
 uint32_t clock_fields=15U|(15U<<(24U+4U*n->config.fimc.csis_index));
 if(n->phase!=CN_BUILDING&&((src&clock_fields)!=(n->cmu_source&clock_fields)||
    (div&clock_fields)!=(n->cmu_divider&clock_fields)))return I9100_CE_OWNER;
 /* Polling owns no GIC handler. Existing enabled users cannot be adopted. */
 const unsigned sources[2]={116U,110U+2U*n->config.fimc.csis_index};
 for(unsigned k=0;k<2;k++){
  uint32_t enabled=rd(n,CN_GIC,0x100U+4U*(sources[k]/32U));
  if(enabled==UINT32_MAX||(enabled&(1U<<(sources[k]%32U))))return I9100_CE_OWNER;
 }
 if(rd(n,CN_SYSMMU,0x18))return I9100_CE_OWNER; /* No unresolved fault. */
 return 0;
}
static int dma_idle(void *v)
{
 i9100_camera_native *n=(i9100_camera_native *)v;
 return resources(n)||!n->fimc.initialized? -1:i9100_fimc_idle(&n->fimc);
}
static int stop(i9100_camera_native *n)
{
 if(n->fimc.consumer)return I9100_CE_BUSY;
 int first=0;
 if(n->csis.owned)first=i9100_csis_stop(&n->csis);
 if(n->fimc.owned){int e=i9100_fimc_stop(&n->fimc);if(!first)first=e;}
 n->cleanup_error=first;return first;
}
static int cleanup(i9100_camera_native *n)
{
 if(n->fimc.consumer)return I9100_CE_BUSY;
 /* On failed acquisition the provider may still own partial resources.
  * No camera MMIO has been attempted until its full validation succeeds. */
 if(n->fimc.owned||n->csis.owned||n->allocation){
  int e=resources(n);if(e)return e;e=stop(n);if(e)return e;
  if(n->fimc.owned){e=i9100_fimc_enable(&n->fimc,0,0);if(e)return e;}
  if(n->csis.owned){e=i9100_csis_enable(&n->csis,0,0);if(e)return e;}
  if(n->allocation){
   i9100_dma_buffer *b=i9100_dma_native_buffer(n->allocation);
   if(b&&b->state==I9100_DMA_QUARANTINED){e=i9100_dma_abort(b);if(e)return e;}
   e=i9100_dma_native_destroy(&n->allocation);if(e)return e;
  }
 }
 if(n->supply_lease.owned){
  int e=n->supply.release(n->supply.cookie,&n->supply_lease);
  if(e||n->supply_lease.owned)return e?e:I9100_CE_OWNER;
 }
 for(unsigned k=0;k<CN_MAPS;k++){
  if(n->mapping[k]){n->mapping[k]->release();n->mapping[k]=0;n->regs[k]=0;}
  if(n->memory[k]){n->memory[k]->release();n->memory[k]=0;}
 }
 if(n->parent_hold.owned){int e=i9100_peripheral_native_hold_put(&n->parent_hold);if(e)return e;}
 if(n->reserved){__sync_lock_release(&fimc0_reserved);n->reserved=0;}return 0;
}
static void dispose(i9100_camera_native *n)
{
 if(n->lifecycle)IOLockFree(n->lifecycle);if(n->fimc_lock)IOLockFree(n->fimc_lock);
 if(n->csis_lock)IOLockFree(n->csis_lock);IOFree(n,sizeof(*n));
}
int i9100_camera_native_open(int enabled,i9100_peripheral_native *parent,const i9100_camera_supply *supply,
 const i9100_camera_native_config *cfg,i9100_camera_native **out)
{
 if(!out||*out||(enabled!=0&&enabled!=1))return I9100_CE_ARGUMENT;if(!enabled)return I9100_CE_DISABLED;
 if(!parent||!supply||!supply->acquire||!supply->validate||!supply->release||!cfg||
    !ml_get_interrupts_enabled()||cfg->csis.width!=cfg->fimc.width||cfg->csis.height!=cfg->fimc.height||
    cfg->csis.width<16U||cfg->csis.width>1920U||(cfg->csis.width&7U)||cfg->csis.height<16U||
    cfg->csis.height>1080U||(cfg->csis.height&1U)||cfg->csis.lanes<1U||cfg->csis.lanes>2U||
    cfg->csis.hs_settle>31U||cfg->csis.alignment!=32U||cfg->csis.external_wclk>1U||cfg->csis.data_type!=0x1eU||
    cfg->fimc.csis_index>1U||cfg->fimc.input_order422>3U||cfg->fimc.alignment!=32U||cfg->fimc.invert_vsync>1U||
    cfg->fimc.timeout_ms<10U||cfg->fimc.timeout_ms>1000U)return I9100_CE_ARGUMENT;
 if(!__sync_bool_compare_and_swap(&fimc0_reserved,0U,1U))return I9100_CE_BUSY;
 i9100_camera_native *n=(i9100_camera_native *)IOMalloc(sizeof(*n));
 if(!n){__sync_lock_release(&fimc0_reserved);return I9100_CE_IO;}
 memset(n,0,sizeof(*n));n->reserved=1;n->parent=parent;n->supply=*supply;n->config=*cfg;n->phase=CN_BUILDING;
 int e=i9100_peripheral_native_hold_get(parent,&n->parent_hold);
 if(e){__sync_lock_release(&fimc0_reserved);dispose(n);return e;}*out=n;
 n->lifecycle=IOLockAlloc();n->fimc_lock=IOLockAlloc();n->csis_lock=IOLockAlloc();
 if(!n->lifecycle||!n->fimc_lock||!n->csis_lock){e=I9100_CE_IO;goto fail;}
 e=supply->acquire(supply->cookie,cfg->fimc.csis_index,&n->supply_lease);if(e)goto fail;
 if(!supply_valid(n)){e=I9100_CE_OWNER;goto fail;}
 {
  const uint32_t physical[CN_MAPS]={0x11800000U,0x11880000U+cfg->fimc.csis_index*0x10000U,
   0x11a20000U,0x10030000U,0x10020000U,0x10490000U};
  const uint32_t lengths[CN_MAPS]={4096,16384,4096,65536,16384,4096};
  for(unsigned k=0;k<CN_MAPS;k++){
   n->memory[k]=IOMemoryDescriptor::withPhysicalAddress(physical[k],lengths[k],kIODirectionInOut);
   n->mapping[k]=n->memory[k]?n->memory[k]->map(kIOMapInhibitCache):0;
   n->regs[k]=n->mapping[k]?(volatile uint8_t *)n->mapping[k]->getAddress():0;
   if(!n->regs[k]){e=I9100_CE_IO;goto fail;}
  }
 }
 e=resources(n);if(e)goto fail;n->cmu_source=rd(n,CN_CMU,0xc220);n->cmu_divider=rd(n,CN_CMU,0xc520);
 {
  i9100_camera_engine_io fio={n,f_read,f_write,f_lock,f_unlock,barrier,ns,delay};
  i9100_camera_engine_io cio={n,c_read,c_write,c_lock,c_unlock,barrier,ns,delay};
  e=i9100_fimc_init(&n->fimc,&fio);if(!e)e=i9100_csis_init(&n->csis,&cio);
  if(!e)e=i9100_fimc_enable(&n->fimc,1,1);if(!e)e=i9100_csis_enable(&n->csis,1,1);if(e)goto fail;
  struct i9100_video_layout layout={};e=i9100_video_layout(I9100_VIDEO_YUYV,cfg->fimc.width,cfg->fimc.height,&layout);if(e)goto fail;
  e=i9100_dma_native_allocate(1,layout.total_bytes,n,dma_idle,&n->allocation);if(e)goto fail;
  e=i9100_dma_enable(i9100_dma_native_buffer(n->allocation),1);if(e)goto fail;
 }
 n->phase=CN_READY;return 0;
fail:n->error=e;n->phase=CN_CLOSING;
 {int off=cleanup(n);n->cleanup_error=off;if(!off){dispose(n);*out=0;}}return e;
}
int i9100_camera_native_start(i9100_camera_native *n)
{
 if(!n)return I9100_CE_ARGUMENT;if(take(n->lifecycle))return I9100_CE_BUSY;
 int e=n->phase!=CN_READY||n->error?I9100_CE_DISABLED:resources(n);
 if(!e)e=i9100_fimc_capture(&n->fimc,&n->config.fimc,i9100_dma_native_buffer(n->allocation));
 if(!e){
  e=i9100_csis_start(&n->csis,&n->config.csis);
  if(e){if(!n->error)n->error=e;if(!resources(n))n->cleanup_error=stop(n);}
 }
 if(e&&e!=I9100_CE_BUSY&&e!=I9100_CE_DISABLED){if(!n->error)n->error=e;if(!resources(n))n->cleanup_error=stop(n);}
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_camera_native_poll(i9100_camera_native *n)
{
 if(!n)return I9100_CE_ARGUMENT;if(take(n->lifecycle))return I9100_CE_BUSY;
 int e=n->phase!=CN_READY?I9100_CE_DISABLED:n->error?n->error:resources(n);
 if(!e&&!n->fimc.ready){
  e=i9100_csis_poll(&n->csis);if(!e)e=i9100_fimc_poll(&n->fimc);
 }
 if(!e&&n->fimc.ready&&(n->csis.running||i9100_csis_idle(&n->csis)))e=i9100_csis_stop(&n->csis);
 if(e&&e!=I9100_CE_BUSY&&e!=I9100_CE_DISABLED){if(!n->error)n->error=e;if(!resources(n))n->cleanup_error=stop(n);}
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_camera_native_take_frame(i9100_camera_native *n,i9100_camera_frame *frame)
{
 if(!n)return I9100_CE_ARGUMENT;if(take(n->lifecycle))return I9100_CE_BUSY;
 int e=n->phase!=CN_READY||n->error?I9100_CE_DISABLED:resources(n);
 if(!e&&(n->csis.running||i9100_csis_idle(&n->csis)))e=I9100_CE_BUSY;
 if(!e)e=i9100_fimc_take_frame(&n->fimc,frame);IOLockUnlock(n->lifecycle);return e;
}
int i9100_camera_native_release_frame(i9100_camera_native *n,i9100_camera_frame *frame)
{
 if(!n)return I9100_CE_ARGUMENT;if(take(n->lifecycle))return I9100_CE_BUSY;
 int e=resources(n);if(!e)e=i9100_fimc_release_frame(&n->fimc,frame);IOLockUnlock(n->lifecycle);return e;
}
int i9100_camera_native_status(i9100_camera_native *n,struct i9100_camera_native_status *out)
{
 if(!n||!out)return I9100_CE_ARGUMENT;if(take(n->lifecycle))return I9100_CE_BUSY;
 i9100_dma_buffer *b=n->allocation?i9100_dma_native_buffer(n->allocation):0;
 memset(out,0,sizeof(*out));out->phase=n->phase;out->supply_generation=n->supply_lease.generation;
 out->csis_running=n->csis.running;out->fimc_running=n->fimc.running;out->frame_ready=n->fimc.ready;
 out->csis_status=n->csis.raw_status;out->csis_errors=n->csis.errors;out->fimc_status=n->fimc.status;out->fimc_status2=n->fimc.status2;
 out->buffer_state=b?b->state:0;out->buffer_holds=b?b->holds:0;out->error=n->error;out->cleanup_error=n->cleanup_error;
 IOLockUnlock(n->lifecycle);return 0;
}
int i9100_camera_native_close(i9100_camera_native **owner)
{
 if(!owner||!*owner)return I9100_CE_ARGUMENT;i9100_camera_native *n=*owner;
 if(n->lifecycle&&take(n->lifecycle))return I9100_CE_BUSY;
 if(n->fimc.consumer){if(n->lifecycle)IOLockUnlock(n->lifecycle);return I9100_CE_BUSY;}
 n->phase=CN_CLOSING;int e=cleanup(n);n->cleanup_error=e;
 if(n->lifecycle)IOLockUnlock(n->lifecycle);if(e)return e;*owner=0;dispose(n);return 0;
}
