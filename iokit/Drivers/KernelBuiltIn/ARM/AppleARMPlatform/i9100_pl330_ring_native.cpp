/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_pl330_ring_native.h"
#include "i9100_dma_native.h"
#include "i9100_peripheral_board.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
uint32_t i9100_ring_mmio_read(volatile uint8_t *,unsigned);
void i9100_ring_mmio_write(volatile uint8_t *,unsigned,uint32_t);
}
#include <string.h>
enum {RN_PDMA,RN_I2S,RN_GIC,RN_MAPS,RN_BUILDING=1,RN_READY=2,RN_CLOSING=3};
struct i9100_pl330_ring_native {
 i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;i9100_ring_consumer consumer;
 IOLock *lifecycle,*engine;
 IOMemoryDescriptor *memory[RN_MAPS];IOMemoryMap *mapping[RN_MAPS];volatile uint8_t *regs[RN_MAPS];
 i9100_clock_lease clock;i9100_pl330_ring ring;
 i9100_pl330_ring_buffer_cookie cookie[4];i9100_dma_native *allocation[4];
 uint32_t phase,fault;int error,cleanup_error,mute_error;
 struct i9100_pl330_ring_status saved;
};
extern "C" __attribute__((noinline)) uint32_t i9100_ring_native_read(void *cookie,unsigned map,unsigned offset)
{
 i9100_pl330_ring_native *n=(i9100_pl330_ring_native *)cookie;
 if(map>=RN_MAPS||!n->regs[map]||(offset&3U)||offset>=4096U)return 0xffffffffU;
 return i9100_ring_mmio_read(n->regs[map],offset);
}
extern "C" __attribute__((noinline)) void i9100_ring_native_write(void *cookie,unsigned map,unsigned offset,uint32_t value)
{
 i9100_pl330_ring_native *n=(i9100_pl330_ring_native *)cookie;
 if(map>=RN_MAPS||!n->regs[map]||(offset&3U)||offset>=4096U)return;
 i9100_ring_mmio_write(n->regs[map],offset,value);
}
static int take(IOLock *lock){return ml_get_interrupts_enabled()&&IOLockTryLock(lock)?0:-1;}
static uint32_t pdma_read(void *c,unsigned off){return i9100_ring_native_read(c,RN_PDMA,off);}
static void pdma_write(void *c,unsigned off,uint32_t value){i9100_ring_native_write(c,RN_PDMA,off,value);}
static int engine_lock(void *c){return take(((i9100_pl330_ring_native *)c)->engine);}
static void engine_unlock(void *c){IOLockUnlock(((i9100_pl330_ring_native *)c)->engine);}
static uint64_t ns(void *){uint64_t value=0;absolutetime_to_nanoseconds(mach_absolute_time(),&value);return value;}
static int delay(void *,uint32_t us){if(!ml_get_interrupts_enabled()||us>100000U)return -1;IODelay(us);return 0;}
static int quiesce(void *cookie)
{
 i9100_pl330_ring_native *n=(i9100_pl330_ring_native *)cookie;
 if(n->consumer.verify_resources(n->consumer.cookie)||i9100_clock_check(&n->clock))return -1;
 n->mute_error=n->consumer.mute(n->consumer.cookie);
 int e=n->consumer.quiesce_requests(n->consumer.cookie);
 if(e||(i9100_ring_native_read(n,RN_I2S,0)&7U))return -1;
 return 0; /* A mute error must not prevent independent request/DMA shutdown. */
}
static int consumer_valid(i9100_pl330_ring_native *n,int running)
{
 if(n->consumer.verify_resources(n->consumer.cookie)||i9100_clock_check(&n->clock))return I9100_RING_OWNER;
 if((i9100_ring_native_read(n,RN_GIC,0x108)&(1U<<3))||
    (i9100_ring_native_read(n,RN_GIC,0x110)&(1U<<1)))return I9100_RING_OWNER;
 uint32_t control=i9100_ring_native_read(n,RN_I2S,0),mode=i9100_ring_native_read(n,RN_I2S,4);
 if((control&((1U<<17)|(1U<<24)|(1U<<26)))||!(control&(1U<<31))||
    (mode&0xc0001fffU)!=0xc0001c00U||i9100_ring_native_read(n,RN_I2S,12))return I9100_RING_OWNER;
 if(running){if((control&(0x7fU|(1U<<18)))!=0x2dU)return I9100_RING_OWNER;}
 else if(control&7U)return I9100_RING_BUSY;
 return 0;
}
static void save(i9100_pl330_ring_native *n)
{if(n->ring.initialized&&n->regs[RN_PDMA]&&n->clock.active&&!i9100_clock_check(&n->clock)&&
 !n->consumer.verify_resources(n->consumer.cookie))(void)i9100_pl330_ring_status(&n->ring,&n->saved);}
static int stop_transport(i9100_pl330_ring_native *n)
{
 int e=0;
 if(n->ring.active||n->ring.held_mask)e=i9100_pl330_ring_stop(&n->ring);
 else n->mute_error=n->consumer.mute(n->consumer.cookie);
 save(n);if(!e&&n->mute_error)e=I9100_RING_IO;n->cleanup_error=e;return e;
}
static int cleanup(i9100_pl330_ring_native *n)
{
 if(n->clock.active&&(i9100_clock_check(&n->clock)||n->consumer.verify_resources(n->consumer.cookie)))return I9100_RING_OWNER;
 if(n->ring.initialized&&n->regs[RN_PDMA]&&n->clock.active){int e=stop_transport(n);if(e)return e;}
 if(n->ring.owned){int e=i9100_pl330_ring_enable(&n->ring,0);if(e)return e;}
 for(unsigned k=0;k<4;k++)if(n->allocation[k]){
  int e=i9100_dma_native_destroy(&n->allocation[k]);if(e)return e;
 }
 if(n->clock.active){int e=i9100_clock_release(&n->clock);if(e)return e;}
 for(unsigned k=0;k<RN_MAPS;k++){
  if(n->mapping[k]){n->mapping[k]->release();n->mapping[k]=0;n->regs[k]=0;}
  if(n->memory[k]){n->memory[k]->release();n->memory[k]=0;}
 }
 if(n->parent_hold.owned){int e=i9100_peripheral_native_hold_put(&n->parent_hold);if(e)return e;}
 return 0;
}
int i9100_pl330_ring_native_open(int enabled,i9100_peripheral_native *parent,const i9100_ring_consumer *consumer,
 uint32_t rate,uint32_t frames,i9100_pl330_ring_native **out)
{
 if(!out||*out||(enabled!=0&&enabled!=1))return I9100_RING_ARGUMENT;
 if(!enabled)return I9100_RING_DISABLED;
 if(!parent||!consumer||!consumer->verify_resources||!consumer->mute||!consumer->start_requests||
    !consumer->quiesce_requests||(rate!=44100U&&rate!=48000U)||frames<16U||frames>512U||
    !ml_get_interrupts_enabled())return I9100_RING_ARGUMENT;
 if(consumer->verify_resources(consumer->cookie))return I9100_RING_OWNER;
 i9100_pl330_ring_native *n=(i9100_pl330_ring_native *)IOMalloc(sizeof(*n));if(!n)return I9100_RING_IO;
 memset(n,0,sizeof(*n));n->parent=parent;n->consumer=*consumer;n->phase=RN_BUILDING;
 int e=i9100_peripheral_native_hold_get(parent,&n->parent_hold);
 if(e){IOFree(n,sizeof(*n));return e;}
 *out=n;n->lifecycle=IOLockAlloc();n->engine=IOLockAlloc();
 if(!n->lifecycle||!n->engine){e=I9100_RING_IO;goto failed;}
 {
  i9100_clock_gates *clocks=i9100_peripheral_native_clocks(parent);
  if(!clocks){e=I9100_RING_OWNER;goto failed;}
  e=i9100_clock_acquire(clocks,I9100_GATE_PDMA0,&n->clock);if(e)goto failed;
 }
 for(unsigned k=0;k<RN_MAPS;k++){
  const uint32_t address[RN_MAPS]={0x12680000U,0x03830000U,0x10490000U};
  n->memory[k]=IOMemoryDescriptor::withPhysicalAddress(address[k],4096,kIODirectionInOut);
  n->mapping[k]=n->memory[k]?n->memory[k]->map(kIOMapInhibitCache):0;
  if(!n->mapping[k]||!n->mapping[k]->getAddress()){e=I9100_RING_IO;goto failed;}
  n->regs[k]=(volatile uint8_t *)n->mapping[k]->getAddress();
 }
 e=consumer_valid(n,0);if(e)goto failed;
 {
  i9100_pl330_ring_io io={{n,pdma_read,pdma_write,engine_lock,engine_unlock,ns,delay},n,quiesce};
  e=i9100_pl330_ring_init(&n->ring,&io);if(e)goto failed;
  e=i9100_pl330_ring_enable(&n->ring,1);if(e)goto failed;
 }
 for(unsigned k=0;k<4;k++){
  e=i9100_pl330_ring_buffer_cookie_init(&n->cookie[k],&n->ring,k);if(e)goto failed;
  e=i9100_dma_native_allocate(1,4096,&n->cookie[k],i9100_pl330_ring_buffer_idle,&n->allocation[k]);if(e)goto failed;
  e=i9100_dma_enable(i9100_dma_native_buffer(n->allocation[k]),1);if(e)goto failed;
 }
 e=i9100_pl330_ring_prepare(&n->ring,i9100_dma_native_buffer(n->allocation[0]),
  i9100_dma_native_buffer(n->allocation[1]),i9100_dma_native_buffer(n->allocation[2]),
  i9100_dma_native_buffer(n->allocation[3]),rate,frames);if(e)goto failed;
 n->phase=RN_READY;return 0;
failed:
 n->error=e;n->phase=RN_CLOSING;n->fault=1;
 {int released=cleanup(n);if(released){n->cleanup_error=released;return e;}}
 if(n->lifecycle)IOLockFree(n->lifecycle);if(n->engine)IOLockFree(n->engine);IOFree(n,sizeof(*n));*out=0;return e;
}
static int run(i9100_pl330_ring_native *n,unsigned op,const int16_t *samples,uint32_t frames,uint32_t *completed)
{
 if(completed)*completed=0;if(!n||!n->lifecycle)return I9100_RING_ARGUMENT;if(take(n->lifecycle))return I9100_RING_BUSY;
 int e=0;
 if(n->phase!=RN_READY||n->fault){e=I9100_RING_FAULT;goto done;}
 e=consumer_valid(n,op!=1U);if(e)goto failed;
 switch(op){
 case 1:
  e=n->consumer.mute(n->consumer.cookie);if(e)goto failed;
  e=i9100_pl330_ring_start(&n->ring);if(e)goto failed;
  e=n->consumer.start_requests(n->consumer.cookie);if(!e)e=consumer_valid(n,1);break;
 case 2:e=i9100_pl330_ring_poll(&n->ring,completed);break;
 case 3:e=i9100_pl330_ring_queue(&n->ring,samples,frames);break;
 case 4:e=i9100_pl330_ring_release_period(&n->ring);break;
 default:e=I9100_RING_ARGUMENT;break;
 }
 if(e&&e!=I9100_RING_BUSY&&e!=I9100_RING_ARGUMENT&&e!=I9100_RING_DISABLED)goto failed;
 goto done;
failed:
 if(!n->fault)n->error=e;n->fault=1;(void)stop_transport(n);
done:save(n);IOLockUnlock(n->lifecycle);return e;
}
int i9100_pl330_ring_native_start(i9100_pl330_ring_native *n){return run(n,1,0,0,0);}
int i9100_pl330_ring_native_poll(i9100_pl330_ring_native *n,uint32_t *out)
{if(!out)return I9100_RING_ARGUMENT;return run(n,2,0,0,out);}
int i9100_pl330_ring_native_queue(i9100_pl330_ring_native *n,const int16_t *samples,uint32_t frames){return run(n,3,samples,frames,0);}
int i9100_pl330_ring_native_release_period(i9100_pl330_ring_native *n){return run(n,4,0,0,0);}
int i9100_pl330_ring_native_status(i9100_pl330_ring_native *n,struct i9100_pl330_ring_status *out)
{
 if(!n||!out||!n->lifecycle)return I9100_RING_ARGUMENT;if(take(n->lifecycle))return I9100_RING_BUSY;
 save(n);*out=n->saved;if(n->fault){out->fault=1;out->error=n->error;}out->cleanup_error=n->cleanup_error;
 IOLockUnlock(n->lifecycle);return 0;
}
int i9100_pl330_ring_native_close(i9100_pl330_ring_native **owner)
{
 if(!owner||!*owner)return I9100_RING_ARGUMENT;i9100_pl330_ring_native *n=*owner;
 if(n->lifecycle&&take(n->lifecycle))return I9100_RING_BUSY;n->phase=RN_CLOSING;
 int e=cleanup(n);if(e){n->cleanup_error=e;if(n->lifecycle)IOLockUnlock(n->lifecycle);return e;}
 if(n->lifecycle){IOLockUnlock(n->lifecycle);IOLockFree(n->lifecycle);}if(n->engine)IOLockFree(n->engine);
 IOFree(n,sizeof(*n));*owner=0;return 0;
}
