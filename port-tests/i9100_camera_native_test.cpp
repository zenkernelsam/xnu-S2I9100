/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "i9100_camera_native.h"
#include "i9100_dma_native.h"
enum {FIMC,CSIS,MMU,CMU,PMU,GIC,MAPS};
static uint32_t regs[MAPS][65536/4];
static const uint32_t addresses[MAPS]={0x11800000U,0x11880000U,0x11a20000U,0x10030000U,0x10020000U,0x10490000U};
static struct model {
 unsigned allocations,fail_at,contexts,locks,maps,descriptors,dma,puts,supply_owned,supply_generation;
 unsigned acquire_error,release_error,verify_error,hold_put_error,drop_map,drop_off,drop_count;
 unsigned writes,freeze,stop_stuck,early_free,syncs,sync_fail;uint64_t ns;
 bool interrupts;
} m;
struct i9100_peripheral_native {unsigned holds;};
static i9100_peripheral_native parent;
static unsigned fail(){return ++m.allocations==m.fail_at;}
void *IOMalloc(size_t n){if(fail())return nullptr;m.contexts++;return calloc(1,n);}
void IOFree(void *p,size_t){assert(p&&m.contexts);m.contexts--;free(p);}
IOLock *IOLockAlloc(){if(fail())return nullptr;m.locks++;return new IOLock{false};}
void IOLockFree(IOLock *l){assert(l&&!l->held&&m.locks);m.locks--;delete l;}
bool IOLockTryLock(IOLock *l){if(l->held)return false;l->held=true;return true;}
void IOLockUnlock(IOLock *l){assert(l->held);l->held=false;}
extern "C" bool ml_get_interrupts_enabled(){return m.interrupts;}
void OSSynchronizeIO(){}
void IODelay(unsigned us){if(!m.freeze)m.ns+=us*1000ULL;}
extern "C" uint64_t mach_absolute_time(){return m.ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t p,size_t n,unsigned dir)
{
 if(fail())return nullptr;assert(dir==kIODirectionInOut);m.descriptors++;
 auto *d=new IOMemoryDescriptor;d->physical=p;d->bytes=n;return d;
}
IOMemoryMap *IOMemoryDescriptor::map(unsigned flags)
{
 if(fail())return nullptr;assert(flags==kIOMapInhibitCache);
 for(unsigned k=0;k<MAPS;k++)if(physical==addresses[k]||(k==CSIS&&physical==addresses[k]+0x10000U)){
  assert(bytes==(k==CSIS||k==PMU?16384U:k==CMU?65536U:4096U));
  m.maps++;auto *map=new IOMemoryMap;map->address=(uintptr_t)regs[k];return map;
 }assert(false);return nullptr;
}
void IOMemoryDescriptor::release(){assert(m.descriptors);m.descriptors--;delete this;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(m.maps);m.maps--;delete this;}
extern "C" uint32_t __real_i9100_camera_mmio_read(volatile uint8_t *,unsigned);
extern "C" void __real_i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
extern "C" uint32_t __wrap_i9100_camera_mmio_read(volatile uint8_t *base,unsigned off)
{return __real_i9100_camera_mmio_read(base,off);}
extern "C" void __wrap_i9100_camera_mmio_write(volatile uint8_t *base,unsigned off,uint32_t value)
{
 unsigned map=MAPS;for(unsigned k=0;k<MAPS;k++)if(base==(volatile uint8_t *)regs[k])map=k;
 assert(map==FIMC||map==CSIS);m.writes++;
 if(m.drop_count&&map==m.drop_map&&off==m.drop_off){m.drop_count--;return;}
 uint32_t old=regs[map][off/4];__real_i9100_camera_mmio_write(base,off,value);
 if(map==CSIS&&off==0x14)regs[map][off/4]=old&~value;
 if(map==CSIS&&off==0)regs[map][0]&=~((1U<<4)|(1U<<16));
 if(map==FIMC&&off==0xc0){
  if(value&(1U<<31))regs[map][0x64/4]|=1U<<22;
  else if(!m.stop_stuck){
   if(regs[map][0x64/4]&(1U<<17))regs[map][0x64/4]|=1U<<16;
   regs[map][0x64/4]&=~((1U<<22)|(1U<<21)|(1U<<26));
  }
 }
 if(map==FIMC&&off==8&&value&(1U<<19)){regs[map][8/4]&=~(1U<<19);regs[map][0x64/4]&=~(1U<<17);}
}
int i9100_peripheral_native_hold_get(i9100_peripheral_native *p,i9100_native_parent_hold *h)
{assert(!h->owned);p->holds++;h->context=p;h->self=(uintptr_t)h;h->owned=1;return 0;}
int i9100_peripheral_native_hold_put(i9100_native_parent_hold *h)
{assert(h->self==(uintptr_t)h&&h->owned&&h->context->holds);if(m.hold_put_error)return -1;h->context->holds--;h->owned=0;m.puts++;return 0;}
struct i9100_dma_native {i9100_dma_buffer b;void *engine;int (*idle)(void *);bool locked;};
static uint8_t pixels[4194304] __attribute__((aligned(64)));
static int d_lock(void *v){auto *d=(i9100_dma_native *)v;if(d->locked)return -1;d->locked=true;return 0;}
static void d_unlock(void *v){auto *d=(i9100_dma_native *)v;assert(d->locked);d->locked=false;}
static int d_idle(void *v){auto *d=(i9100_dma_native *)v;return d->idle(d->engine);}
static int d_sync(void *v,uintptr_t cpu,uint32_t pa,uint32_t bytes,unsigned action)
{auto *d=(i9100_dma_native *)v;assert(d->locked&&cpu==(uintptr_t)pixels&&pa==0x46000000U&&bytes==d->b.bytes);
 if(action==I9100_DMA_INVALIDATE)assert(!d_idle(d));return ++m.syncs==m.sync_fail?-1:0;}
static void d_free(void *v,uintptr_t,uint32_t,uint32_t){auto *d=(i9100_dma_native *)v;assert(!d_idle(d));}
int i9100_dma_native_allocate(int enabled,uint32_t bytes,void *engine,int (*idle)(void *),i9100_dma_native **out)
{
 assert(enabled==1&&bytes<=sizeof(pixels)&&!m.dma&&out&&!*out);if(fail())return I9100_DMA_IO;
 auto *d=new i9100_dma_native{};d->engine=engine;d->idle=idle;memset(pixels,0,sizeof(pixels));
 i9100_dma_io io={d,d_lock,d_unlock,d_sync,d_idle,d_free};
 assert(!i9100_dma_bind(&d->b,&io,(uintptr_t)pixels,0x46000000U,bytes));m.dma++;*out=d;return 0;
}
i9100_dma_buffer *i9100_dma_native_buffer(i9100_dma_native *d){return d?&d->b:nullptr;}
int i9100_dma_native_destroy(i9100_dma_native **out)
{
 assert(out&&*out&&m.dma);auto *d=*out;int e=i9100_dma_release(&d->b);
 if(e)return e;m.dma--;delete d;*out=nullptr;return 0;
}
static int acquire(void *,unsigned index,i9100_camera_supply_lease *l)
{
 assert(!m.supply_owned&&!l->owned);m.supply_owned=1;m.supply_generation++;
 l->self=(uintptr_t)l;l->provider=(uintptr_t)&m;l->generation=m.supply_generation;l->owned=1;l->csis_index=index;
 regs[CMU][0xc920/4]=(1U<<7)|1U|(1U<<(4U+index));regs[PMU][(0x710+index*4U)/4]=7U;
 return m.acquire_error?-123:0;
}
static int validate(void *,const i9100_camera_supply_lease *l)
{
 if(m.verify_error)return -1;
 return l->self!=(uintptr_t)l||l->provider!=(uintptr_t)&m||!l->owned||!m.supply_owned||l->generation!=m.supply_generation?-1:0;
}
static int release(void *,i9100_camera_supply_lease *l)
{
 assert(l->self==(uintptr_t)l&&m.supply_owned&&l->owned);
 if(m.release_error)return -124;regs[CMU][0xc920/4]=0;regs[PMU][(0x710+l->csis_index*4U)/4]&=~3U;
 l->owned=0;m.supply_owned=0;return 0;
}
static const i9100_camera_supply supply={nullptr,acquire,validate,release};
static const i9100_camera_native_config config={{640,480,2,12,32,1,0x1e},{640,480,0,2,32,1,500}};
static void setup()
{
 assert(!m.contexts&&!m.maps&&!m.descriptors&&!m.dma&&!m.locks&&!m.supply_owned&&!parent.holds);
 memset(&m,0,sizeof(m));memset(regs,0,sizeof(regs));m.interrupts=true;m.ns=1000000ULL;
 regs[FIMC][8/4]=1U<<30;regs[CSIS][0]=1U<<31;
}
static void arrived()
{
 assert(regs[FIMC][0xc0/4]&(1U<<31)&&regs[CSIS][0]&1U&&m.dma);
 for(unsigned k=0;k<614400U;k++)pixels[k]=(uint8_t)(k*3U+9U);
 regs[FIMC][0x64/4]|=1U<<17;regs[FIMC][0x68/4]=1U<<7;
}
static void finish(i9100_camera_native **n)
{
 int e=i9100_camera_native_close(n);
 if(e)fprintf(stderr,"close=%d maps=%u dma=%u supply=%u status=%x cap=%x seq=%x\n",e,m.maps,m.dma,m.supply_owned,regs[FIMC][0x64/4],regs[FIMC][0xc0/4],regs[FIMC][0x1fc/4]);
 assert(!e&&!*n&&!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!m.dma&&!m.supply_owned&&!parent.holds);
}
int main()
{
 setup();i9100_camera_native *n=nullptr,*second=nullptr;
 assert(i9100_camera_native_open(0,nullptr,nullptr,nullptr,&n)==I9100_CE_DISABLED&&!m.allocations);
 assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));assert(m.maps==6&&m.dma==1&&parent.holds==1);
 assert(i9100_camera_native_open(1,&parent,&supply,&config,&second)==I9100_CE_BUSY&&!second);
 assert(!i9100_camera_native_start(n));assert(i9100_camera_native_poll(n)==I9100_CE_BUSY);
 arrived();assert(!i9100_camera_native_poll(n));assert(!(regs[CSIS][0]&1U)&&!(regs[FIMC][0xc0/4]&(1U<<31)));
 i9100_camera_frame frame={};assert(!i9100_camera_native_take_frame(n,&frame));
 assert(frame.bytes==614400U&&frame.buffer->state==I9100_DMA_CPU&&frame.buffer->holds==1);
 for(unsigned k=0;k<frame.bytes;k++)assert(((uint8_t *)frame.buffer->cpu)[k]==(uint8_t)(k*3U+9U));
 assert(i9100_camera_native_close(&n)==I9100_CE_BUSY&&n);
 assert(!i9100_camera_native_release_frame(n,&frame));assert(!i9100_camera_native_start(n));arrived();assert(!i9100_camera_native_poll(n));finish(&n);
 for(unsigned k=1;k<=17;k++){
  setup();m.fail_at=k;int e=i9100_camera_native_open(1,&parent,&supply,&config,&n);
  if(e)assert(!n&&!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!m.dma&&!m.supply_owned&&!parent.holds);
  else finish(&n);
 }
 setup();m.acquire_error=m.release_error=1;assert(i9100_camera_native_open(1,&parent,&supply,&config,&n)==-123&&n&&m.supply_owned);
 assert(i9100_camera_native_close(&n)==-124&&n);m.release_error=0;finish(&n);
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));m.verify_error=1;
 unsigned writes=m.writes;assert(i9100_camera_native_start(n)==I9100_CE_OWNER&&m.writes==writes);
 assert(i9100_camera_native_close(&n)==I9100_CE_OWNER&&n&&m.dma==1);m.verify_error=0;finish(&n);
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));assert(!i9100_camera_native_start(n));arrived();m.stop_stuck=m.freeze=1;
 assert(i9100_camera_native_poll(n)==I9100_CE_TIMEOUT&&n&&m.dma==1);
 struct i9100_camera_native_status status={};assert(!i9100_camera_native_status(n,&status));
 assert(status.error==I9100_CE_TIMEOUT&&status.buffer_state==I9100_DMA_QUARANTINED&&status.buffer_holds==1);
 assert(i9100_camera_native_close(&n)!=0&&n&&m.maps==6&&m.supply_owned);m.stop_stuck=0;finish(&n);
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));m.release_error=1;
 assert(i9100_camera_native_close(&n)==-124&&n&&!m.dma&&m.maps==6);m.release_error=0;finish(&n);
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));m.hold_put_error=1;
 assert(i9100_camera_native_close(&n)==-1&&n&&!m.maps&&!m.dma&&!m.supply_owned&&parent.holds==1);m.hold_put_error=0;finish(&n);
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));m.sync_fail=m.syncs+1U;
 assert(i9100_camera_native_close(&n)==I9100_DMA_IO&&n&&m.dma==1&&m.supply_owned);
 m.sync_fail=0;finish(&n); /* Explicit close reclaims only after real idle. */
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));m.drop_map=CSIS;m.drop_off=0x2c;m.drop_count=1;
 assert(i9100_camera_native_start(n)==I9100_CE_IO&&!(regs[FIMC][0xc0/4]&(1U<<31)));finish(&n);
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));regs[CMU][0xc520/4]^=1U;
 assert(i9100_camera_native_start(n)==I9100_CE_OWNER);regs[CMU][0xc520/4]^=1U;finish(&n);
 setup();assert(!i9100_camera_native_open(1,&parent,&supply,&config,&n));regs[CMU][0xc520/4]^=1U<<8;
 assert(!i9100_camera_native_start(n));arrived();assert(!i9100_camera_native_poll(n));finish(&n);
 assert(regs[CMU][0xc520/4]&(1U<<8)); /* Other FIMC source fields remain foreign. */
 setup();m.interrupts=false;assert(i9100_camera_native_open(1,&parent,&supply,&config,&n)==I9100_CE_ARGUMENT&&!m.allocations);
 puts("PASS actual camera native CSIS/FIMC pipeline with explicit IOKit/allocator/supply/MMIO facades: single reservation, 6 maps, pinned frame, repeat capture, partial acquisition and failed-stop/close retention");
 return 0;
}
