/* SPDX-License-Identifier: BSD-2-Clause; real native control + actual ring VM. */
#define main ring_core_reference_main
#include "i9100_pl330_ring_test.c"
#undef main
#include "i9100_pl330_ring_native.h"
#include "i9100_dma_native.h"
#include "offline-native-sdk/i9100_model_iokit.h"
#include <stdlib.h>
struct i9100_peripheral_native {i9100_clock_gates clocks;unsigned holds;};
struct i9100_dma_native {unsigned slot;};
static struct {
 uint32_t clock_regs[65536/4],i2s[1024],gic[1024];
 unsigned allocations,fail_at,contexts,locks,descriptors,maps,dma_count,clock_locked;
 unsigned verify_bad,mute_bad,start_bad,quiesce_bad,put_bad,drop_clock_write,slots;
 void *engine_cookie[4];int (*engine_idle[4])(void *);
} native;
static i9100_peripheral_native parent;
static bool allocation_fails(){return ++native.allocations==native.fail_at;}
void *IOMalloc(size_t bytes){if(allocation_fails())return nullptr;native.contexts++;return calloc(1,bytes);}
void IOFree(void *p,size_t){assert(native.contexts);native.contexts--;free(p);}
IOLock *IOLockAlloc(){if(allocation_fails())return nullptr;native.locks++;return new IOLock{false};}
void IOLockFree(IOLock *p){assert(native.locks&&!p->held);native.locks--;delete p;}
bool IOLockTryLock(IOLock *p){if(p->held)return false;p->held=true;return true;}
void IOLockUnlock(IOLock *p){assert(p->held);p->held=false;}
void OSSynchronizeIO(){}
void IODelay(unsigned us){(void)delay(&m,us);}
extern "C" int ml_get_interrupts_enabled(){return 1;}
extern "C" uint64_t mach_absolute_time(){return m.ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t address,size_t bytes,unsigned)
{
 if(allocation_fails())return nullptr;assert(bytes==4096);native.descriptors++;
 auto *p=new IOMemoryDescriptor;p->physical=address;p->bytes=bytes;return p;
}
IOMemoryMap *IOMemoryDescriptor::map(unsigned flags)
{
 assert(flags==kIOMapInhibitCache);if(allocation_fails())return nullptr;native.maps++;auto *p=new IOMemoryMap;
 if(physical==0x12680000U)p->address=(uintptr_t)m.regs;
 else if(physical==0x03830000U)p->address=(uintptr_t)native.i2s;
 else {assert(physical==0x10490000U);p->address=(uintptr_t)native.gic;}return p;
}
void IOMemoryDescriptor::release(){assert(native.descriptors);native.descriptors--;delete this;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(native.maps);native.maps--;delete this;}
static int native_buffer_idle(void *cookie)
{
 auto *b=(ring_buffer *)cookie;unsigned slot=(unsigned)(b-m.buffers);assert(slot<4);
 return native.engine_idle[slot](native.engine_cookie[slot]);
}
int i9100_dma_native_allocate(int enabled,uint32_t bytes,void *engine,int (*idle_fn)(void *),i9100_dma_native **out)
{
 assert(enabled==1&&bytes==4096&&native.slots<4);*out=nullptr;
 auto *p=(i9100_dma_native *)IOMalloc(sizeof(i9100_dma_native));if(!p)return I9100_DMA_IO;
 p->slot=native.slots++;native.engine_cookie[p->slot]=engine;native.engine_idle[p->slot]=idle_fn;
 auto *cookie=(i9100_pl330_ring_buffer_cookie *)engine;m.ring=cookie->ring;
 auto *b=&m.buffers[p->slot];i9100_dma_io io={b,block_lock,block_unlock,sync_buffer,native_buffer_idle,release_buffer};
 assert(!i9100_dma_bind(&b->b,&io,(uintptr_t)memory_bytes[p->slot],pa(p->slot),bytes));
 native.dma_count++;*out=p;return 0;
}
i9100_dma_buffer *i9100_dma_native_buffer(i9100_dma_native *p){return &m.buffers[p->slot].b;}
int i9100_dma_native_destroy(i9100_dma_native **owner)
{int e=i9100_dma_release(i9100_dma_native_buffer(*owner));if(e)return e;native.dma_count--;IOFree(*owner,sizeof(**owner));*owner=nullptr;return 0;}
int i9100_peripheral_native_hold_get(i9100_peripheral_native *p,i9100_native_parent_hold *h)
{assert(!h->owned);h->context=p;h->self=(uintptr_t)h;h->owned=1;p->holds++;return 0;}
int i9100_peripheral_native_hold_put(i9100_native_parent_hold *h)
{assert(h->self==(uintptr_t)h&&h->owned&&h->context->holds);if(native.put_bad)return -1;h->context->holds--;h->owned=0;return 0;}
i9100_clock_gates *i9100_peripheral_native_clocks(i9100_peripheral_native *p){return &p->clocks;}
static int clock_lock(void *){if(native.clock_locked)return -1;native.clock_locked=1;return 0;}
static void clock_unlock(void *){assert(native.clock_locked);native.clock_locked=0;}
static uint32_t clock_read(void *,unsigned off){assert(native.clock_locked);return native.clock_regs[off/4];}
static void clock_write(void *,unsigned off,uint32_t value)
{assert(native.clock_locked);if(!native.drop_clock_write)native.clock_regs[off/4]=value;}
extern "C" uint32_t __real_i9100_ring_mmio_read(volatile uint8_t *,unsigned);
extern "C" void __real_i9100_ring_mmio_write(volatile uint8_t *,unsigned,uint32_t);
extern "C" uint32_t __wrap_i9100_ring_mmio_read(volatile uint8_t *base,unsigned off)
{return base==(volatile uint8_t *)m.regs?read32(&m,off):__real_i9100_ring_mmio_read(base,off);}
extern "C" void __wrap_i9100_ring_mmio_write(volatile uint8_t *base,unsigned off,uint32_t value)
{
 __real_i9100_ring_mmio_write(base,off,value);
 if(base==(volatile uint8_t *)m.regs){unsigned old=m.locked;m.locked=1;write32(&m,off,value);m.locked=old;}
}
static int verify(void *){return native.verify_bad?-1:0;}
static int mute(void *){return native.mute_bad?-1:0;}
static int start_requests(void *)
{if(!native.start_bad){native.i2s[0]=0x8000002dU;m.requests=1;vm(0);}return 0;}
static int stop_requests(void *)
{if(!native.quiesce_bad){native.i2s[0]&=~7U;m.requests=0;}return 0;}
static void hw_setup()
{
 assert(!native.contexts&&!native.locks&&!native.maps&&!native.descriptors&&!native.dma_count&&!parent.holds);
 memset(&native,0,sizeof(native));memset(&parent,0,sizeof(parent));memset(&m,0,sizeof(m));m.ns=1000000ULL;m.drop_debug_op=0xffffffffU;
 uint32_t id[2]={0x41330U,0xb105f00dU};for(unsigned k=0;k<2;k++)for(unsigned n=0;n<4;n++)m.regs[((k?0xff0:0xfe0)+n*4U)/4]=(id[k]>>(n*8U))&255U;
 m.regs[0xe00/4]=1U|(7U<<4)|(31U<<12)|(31U<<17);m.regs[0xe0c/4]=1U<<12;m.regs[0xe10/4]=IRQ3|GATE4;
 native.i2s[0]=0x80000000U;native.i2s[1]=0xc0001c00U;
 i9100_clock_io cio={nullptr,clock_read,clock_write,clock_lock,clock_unlock};
 assert(!i9100_clock_init(&parent.clocks,&cio,i9100_u1_gates,I9100_GATE_COUNT));assert(!i9100_clock_enable(&parent.clocks,1));
}
static i9100_ring_consumer consumer={nullptr,verify,mute,start_requests,stop_requests};
static void finish_native(i9100_pl330_ring_native **n)
{
 assert(!i9100_pl330_ring_native_close(n)&&!*n&&!native.contexts&&!native.locks&&!native.maps&&!native.descriptors&&!native.dma_count&&!parent.holds);
 assert(!(native.clock_regs[0xc940/4]&1U)&&!(m.regs[0x20/4]&(IRQ3|GATE4)));
}
int main()
{
 hw_setup();i9100_pl330_ring_native *n=nullptr;
 assert(i9100_pl330_ring_native_open(0,nullptr,nullptr,0,0,&n)==I9100_RING_DISABLED&&!native.allocations);
 assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));
 assert(native.maps==3&&native.dma_count==4&&parent.holds==1);
 int start_error=i9100_pl330_ring_native_start(n);
 if(start_error){struct i9100_pl330_ring_status debug={};(void)i9100_pl330_ring_native_status(n,&debug);
  fprintf(stderr,"native start=%d primary=%d cleanup=%d ringphase=%u cs=%x pc=%x events=%x inten=%x held=%x\n",
   start_error,debug.error,debug.cleanup_error,debug.phase,debug.cs,debug.cpc,debug.events,debug.interrupts,debug.held_mask);}
 assert(!start_error);
 uint32_t completed=0;assert(!i9100_pl330_ring_native_poll(n,&completed)&&completed==1);
 int16_t samples[128];for(unsigned k=0;k<128;k++)samples[k]=(int16_t)(k*491U);
 for(unsigned k=0;k<64;k++){
  assert(!i9100_pl330_ring_native_queue(n,samples,64));assert(!i9100_pl330_ring_native_release_period(n));
  m.ns+=1000000ULL;assert(!i9100_pl330_ring_native_poll(n,&completed)&&completed==1);
 }
 struct i9100_pl330_ring_status status={};assert(!i9100_pl330_ring_native_status(n,&status)&&status.completed_periods==65U);
 assert(m.fifo_words==4160U);finish_native(&n);
 for(unsigned point=1;point<=13U;point++){
  hw_setup();native.fail_at=point;int error=i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n);assert(error&&!n);
  assert(!native.contexts&&!native.locks&&!native.maps&&!native.descriptors&&!native.dma_count&&!parent.holds);
 }
 /* Callback success is insufficient if actual requests did not start/stop. */
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));native.start_bad=1;
 assert(i9100_pl330_ring_native_start(n)==I9100_RING_OWNER);native.start_bad=0;finish_native(&n);
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));assert(!i9100_pl330_ring_native_start(n));native.quiesce_bad=1;
 assert(i9100_pl330_ring_native_close(&n)!=0&&n&&native.dma_count==4&&parent.holds==1&&!m.kills);
 native.quiesce_bad=0;finish_native(&n);
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));assert(!i9100_pl330_ring_native_start(n));native.mute_bad=1;
 assert(i9100_pl330_ring_native_close(&n)!=0&&n&&native.dma_count==4&&!(m.regs[0x100/4]&15U));
 native.mute_bad=0;finish_native(&n);
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));assert(!i9100_pl330_ring_native_start(n));m.kill_stuck=1;
 assert(i9100_pl330_ring_native_close(&n)==I9100_RING_TIMEOUT&&n&&native.maps==3&&parent.holds==1);
 m.kill_stuck=0;finish_native(&n);
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));native.put_bad=1;
 assert(i9100_pl330_ring_native_close(&n)!=0&&n&&!native.maps&&!native.dma_count&&parent.holds==1);
 native.put_bad=0;finish_native(&n);
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));assert(!i9100_pl330_ring_native_start(n));
 native.verify_bad=1;unsigned reads=m.reads;
 assert(i9100_pl330_ring_native_poll(n,&completed)==I9100_RING_OWNER&&m.reads==reads&&parent.holds==1);
 assert(i9100_pl330_ring_native_close(&n)==I9100_RING_OWNER&&n&&native.maps==3);
 native.verify_bad=0;finish_native(&n);
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));assert(!i9100_pl330_ring_native_start(n));
 native.gic[0x108/4]|=1U<<3;
 assert(i9100_pl330_ring_native_poll(n,&completed)==I9100_RING_OWNER&&!completed&&!(m.regs[0x100/4]&15U));
 finish_native(&n);assert(native.gic[0x108/4]&(1U<<3)); /* foreign GIC bit was not rewritten */
 hw_setup();assert(!i9100_pl330_ring_native_open(1,&parent,&consumer,48000,64,&n));assert(!i9100_pl330_ring_native_start(n));
 native.i2s[0]|=1U<<17;
 assert(i9100_pl330_ring_native_poll(n,&completed)==I9100_RING_OWNER&&!completed&&!(m.regs[0x100/4]&15U));
 finish_native(&n);assert(native.i2s[0]&(1U<<17)); /* no false FIFO ACK */
 puts("PASS real ring native IO/control with register/DMA allocator facades: actual core VM, zero start, 4 buffers, start/stop readback, failure retention, parent/clock lifetime");return 0;
}
