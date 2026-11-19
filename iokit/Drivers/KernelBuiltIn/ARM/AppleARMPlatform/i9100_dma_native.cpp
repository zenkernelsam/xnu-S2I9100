/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_dma_native.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <mach/vm_param.h>
#include <string.h>
extern "C" {
#include <vm/pmap.h>
vm_offset_t pmap_pte(pmap_t,vm_offset_t);
void pmap_set_cache_attributes(ppnum_t,unsigned int);
extern unsigned long gPhysBase,gVirtBase;
}
struct i9100_dma_native {
 IOBufferMemoryDescriptor *memory;
 IOLock *lock;
 i9100_dma_buffer buffer;
 void *engine_cookie;
 int (*engine_idle)(void *);
 bool prepared,released;
};
static int take(void *cookie)
{return IOLockTryLock(((i9100_dma_native *)cookie)->lock)?0:-1;}
static void drop(void *cookie){IOLockUnlock(((i9100_dma_native *)cookie)->lock);}
static int idle(void *cookie)
{i9100_dma_native *n=(i9100_dma_native *)cookie;return n->engine_idle(n->engine_cookie);}
static int synchronize(void *cookie,uintptr_t cpu,uint32_t pa,uint32_t bytes,unsigned action)
{
 i9100_dma_native *n=(i9100_dma_native *)cookie;
 if(cpu!=n->buffer.cpu||pa!=n->buffer.physical||bytes!=n->buffer.bytes||
    action<I9100_DMA_CLEAN||action>I9100_DMA_BARRIER)return -1;
 /* Normal-NC allocation and verified aliases make range cache invalidation
  * unnecessary. Do not call invalidate_dcache(): this port's symbol means
  * invalidate-all and would discard unrelated dirty kernel/IPC memory. */
 __asm__ volatile("dsb sy" ::: "memory");
 return 0;
}
static void release(void *cookie,uintptr_t,uint32_t,uint32_t)
{
 i9100_dma_native *n=(i9100_dma_native *)cookie;
 if(n->prepared){n->memory->complete(kIODirectionInOut);n->prepared=false;}
 n->memory->release();n->memory=0;n->released=true;
}
int i9100_dma_native_allocate(int enabled,uint32_t bytes,void *engine_cookie,int (*engine_idle)(void *),i9100_dma_native **result)
{return i9100_dma_native_allocate_aligned(enabled,bytes,4096,engine_cookie,engine_idle,result);}
int i9100_dma_native_allocate_aligned(int enabled,uint32_t bytes,uint32_t alignment,void *engine_cookie,int (*engine_idle)(void *),i9100_dma_native **result)
{
 if(!result)return I9100_DMA_ARGUMENT;*result=0;
 if(enabled!=1)return I9100_DMA_DISABLED;
 if(!engine_idle||!bytes||(bytes&4095U)||bytes>8U*1024U*1024U||
    (alignment!=4096U&&alignment!=8192U&&alignment!=16384U&&alignment!=32768U&&
     alignment!=65536U&&alignment!=131072U))return I9100_DMA_ARGUMENT;
 i9100_dma_native *n=(i9100_dma_native *)IOMalloc(sizeof(*n));
 if(!n)return I9100_DMA_IO;
 memset(n,0,sizeof(*n));n->engine_cookie=engine_cookie;n->engine_idle=engine_idle;
 n->lock=IOLockAlloc();
 if(!n->lock){IOFree(n,sizeof(*n));return I9100_DMA_IO;}
 n->memory=IOBufferMemoryDescriptor::withOptions(kIODirectionInOut|
    kIOMemoryPhysicallyContiguous|kIOMapInhibitCache,bytes,alignment);
 n->prepared=n->memory&&n->memory->prepare(kIODirectionInOut)==kIOReturnSuccess;
 IOByteCount span=0;addr64_t pa=n->prepared?n->memory->getPhysicalSegment(0,&span):0;
 void *cpu=n->memory?n->memory->getBytesNoCopy():0;
 bool valid=n->prepared&&cpu&&pa>=0x40000000ULL&&pa+bytes<=0x80000000ULL&&
    !(pa&(alignment-1U))&&!((uintptr_t)cpu&(alignment-1U))&&span>=bytes;
 if(valid)for(unsigned off=0;off<bytes;off+=4096U){
  pmap_set_cache_attributes((ppnum_t)((pa+off)>>12),VM_MEM_COHERENT|VM_MEM_NOT_CACHEABLE|VM_MEM_GUARDED);
  vm_offset_t ptep=pmap_pte(kernel_pmap,(vm_offset_t)cpu+off);
  uint32_t value=ptep?*(volatile uint32_t *)ptep:0;
  if(!(value&2U)||(value&0xfffff000U)!=(uint32_t)(pa+off)||(value&0x1ccU)!=0x40U)valid=false;
  /* The original linear kernel alias is also retained by this port. */
  vm_offset_t linear=((vm_offset_t)pa+off-gPhysBase)+gVirtBase;
  vm_offset_t lpte=pmap_pte(kernel_pmap,linear);
  value=lpte?*(volatile uint32_t *)lpte:0;
  if(!(value&2U)||(value&0xfffff000U)!=(uint32_t)(pa+off)||(value&0x1ccU)!=0x40U)valid=false;
 }
 int error=I9100_DMA_IO;
 if(valid){
  i9100_dma_io io;memset(&io,0,sizeof(io));io.cookie=n;io.lock=take;io.unlock=drop;
  io.sync=synchronize;io.is_idle=idle;io.release=release;
  error=i9100_dma_bind(&n->buffer,&io,(uintptr_t)cpu,(uint32_t)pa,bytes);
  if(!error){memset(cpu,0,bytes);__asm__ volatile("dsb sy" ::: "memory");*result=n;return 0;}
 }
 if(n->prepared)n->memory->complete(kIODirectionInOut);
 if(n->memory)n->memory->release();IOLockFree(n->lock);IOFree(n,sizeof(*n));return error;
}
struct i9100_dma_buffer *i9100_dma_native_buffer(i9100_dma_native *n)
{return n&&!n->released?&n->buffer:0;}
int i9100_dma_native_destroy(i9100_dma_native **owner)
{
 if(!owner||!*owner)return I9100_DMA_ARGUMENT;
 i9100_dma_native *n=*owner;
 int e=i9100_dma_release(&n->buffer);if(e)return e;
 if(!n->released)return I9100_DMA_IO;
 IOLockFree(n->lock);IOFree(n,sizeof(*n));*owner=0;return 0;
}
