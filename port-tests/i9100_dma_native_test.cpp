/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <mach/vm_param.h>
#include <vm/pmap.h>
#include "i9100_dma_native.h"
static struct model {
 unsigned allocations,fail_at,contexts,locks,descriptors,prepared,attributes,barriers,misalign_cpu,misalign_pa;
 unsigned short_span,non_normal,bad_linear,busy;
 uint32_t ptes[2048],linear[2048];
} m;
static uint8_t memory[8U*1024U*1024U+131072U] __attribute__((aligned(131072)));
static unsigned fail(){return ++m.allocations==m.fail_at;}
void *IOMalloc(size_t n){if(fail())return nullptr;m.contexts++;return calloc(1,n);}
void IOFree(void *p,size_t){assert(m.contexts);m.contexts--;free(p);}
IOLock *IOLockAlloc(){if(fail())return nullptr;m.locks++;return new IOLock{false};}
void IOLockFree(IOLock *l){assert(!l->held&&m.locks);m.locks--;delete l;}
bool IOLockTryLock(IOLock *l){if(l->held)return false;l->held=true;return true;}
void IOLockUnlock(IOLock *l){assert(l->held);l->held=false;}
void OSMemoryBarrier(){m.barriers++;}
IOBufferMemoryDescriptor *IOBufferMemoryDescriptor::withOptions(unsigned options,size_t bytes,size_t alignment)
{
 if(fail())return nullptr;assert((options&(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapInhibitCache))==options);
 assert(alignment>=4096U&&alignment<=131072U);m.descriptors++;
 auto *d=new IOBufferMemoryDescriptor;d->bytes=bytes;d->data=memory+(m.misalign_cpu?4096U:0U);
 d->pa=0x46000000U+(m.misalign_pa?4096U:0U);d->prepared=0;return d;
}
IOReturn IOBufferMemoryDescriptor::prepare(unsigned direction)
{assert(direction==kIODirectionInOut&&!prepared);if(fail())return -1;m.prepared++;prepared=1;return 0;}
IOReturn IOBufferMemoryDescriptor::complete(unsigned direction)
{assert(direction==kIODirectionInOut&&prepared&&m.prepared);prepared=0;m.prepared--;return 0;}
addr64_t IOBufferMemoryDescriptor::getPhysicalSegment(unsigned offset,IOByteCount *span)
{assert(!offset&&prepared);*span=m.short_span?bytes-4096U:bytes;return pa;}
void *IOBufferMemoryDescriptor::getBytesNoCopy(){return data;}
void IOBufferMemoryDescriptor::release(){assert(!prepared&&m.descriptors);m.descriptors--;delete this;}
extern "C" {
unsigned long gPhysBase=0x40000000U,gVirtBase=0x80000000U;
pmap_t kernel_pmap=(void *)1;
void pmap_set_cache_attributes(ppnum_t pp,unsigned int flags)
{
 assert(flags==(VM_MEM_COHERENT|VM_MEM_NOT_CACHEABLE|VM_MEM_GUARDED));
 unsigned i=pp-(0x46000000U>>12);assert(i<2048U);m.attributes++;
 m.ptes[i]=(pp<<12)|2U|(m.non_normal?0U:0x40U);m.linear[i]=(pp<<12)|2U|(m.bad_linear?0U:0x40U);
}
vm_offset_t pmap_pte(pmap_t map,vm_offset_t va)
{
 assert(map==kernel_pmap);uintptr_t base=(uintptr_t)memory;
 if(va>=base&&va<base+8U*1024U*1024U)return (uintptr_t)&m.ptes[(va-base)/4096U];
 if(va>=0x86000000U&&va<0x86800000U)return (uintptr_t)&m.linear[(va-0x86000000U)/4096U];
 return 0;
}
}
static int idle(void *){return m.busy?-1:0;}
static void setup(){assert(!m.contexts&&!m.locks&&!m.descriptors&&!m.prepared);memset(&m,0,sizeof(m));memset(memory,0xa5,sizeof(memory));}
int main()
{
 setup();i9100_dma_native *n=nullptr;
 assert(i9100_dma_native_allocate_aligned(0,4096,131072,nullptr,idle,&n)==I9100_DMA_DISABLED&&!m.allocations);
 const unsigned alignments[]={4096,8192,16384,32768,65536,131072};
 for(unsigned i=0;i<6;i++){
  setup();assert(!i9100_dma_native_allocate_aligned(1,8U*1024U*1024U,alignments[i],nullptr,idle,&n));
  auto *b=i9100_dma_native_buffer(n);assert(b&&b->state==I9100_DMA_BOUND&&!(b->physical&(alignments[i]-1U))&&!(b->cpu&(alignments[i]-1U)));
  assert(m.attributes==2048U&&m.prepared==1U&&m.barriers);
  for(unsigned k=0;k<8U*1024U*1024U;k++)assert(memory[k]==0U);
  assert(!i9100_dma_enable(b,1));assert(!i9100_dma_prepare(b,I9100_DMA_BIDIRECTIONAL));
  m.busy=1;assert(i9100_dma_native_destroy(&n)==I9100_DMA_OWNER&&n&&m.descriptors==1);
  assert(i9100_dma_abort(b)==I9100_DMA_BUSY&&b->state==I9100_DMA_QUARANTINED);m.busy=0;
  assert(!i9100_dma_abort(b));assert(!i9100_dma_native_destroy(&n)&&!n&&!m.prepared&&!m.descriptors);
 }
 for(unsigned i=1;i<=4;i++){
  setup();m.fail_at=i;assert(i9100_dma_native_allocate_aligned(1,131072,131072,nullptr,idle,&n)!=0&&!n);
  assert(!m.contexts&&!m.locks&&!m.descriptors&&!m.prepared);
 }
 setup();m.misalign_cpu=1;assert(i9100_dma_native_allocate_aligned(1,131072,131072,nullptr,idle,&n)==I9100_DMA_IO&&!n);
 setup();m.misalign_pa=1;assert(i9100_dma_native_allocate_aligned(1,131072,131072,nullptr,idle,&n)==I9100_DMA_IO&&!n);
 setup();m.short_span=1;assert(i9100_dma_native_allocate_aligned(1,131072,131072,nullptr,idle,&n)==I9100_DMA_IO&&!n);
 setup();m.non_normal=1;assert(i9100_dma_native_allocate_aligned(1,131072,131072,nullptr,idle,&n)==I9100_DMA_IO&&!n);
 setup();m.bad_linear=1;assert(i9100_dma_native_allocate_aligned(1,131072,131072,nullptr,idle,&n)==I9100_DMA_IO&&!n);
 setup();assert(i9100_dma_native_allocate_aligned(1,131072,262144,nullptr,idle,&n)==I9100_DMA_ARGUMENT&&!m.allocations);
 puts("PASS actual DMA native allocation with explicit IOKit/PTE/DSB facades: page-through128KiB, CPU+physical alignment, contiguous span, both NC aliases, 8MiB, all construction failures, device/quarantine retention");return 0;
}
