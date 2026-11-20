/* SPDX-License-Identifier: BSD-2-Clause
 * Actual allocator is linked; host replacements cover only IOKit memory,
 * PTEs and DSB. Normal legacy models never invoke these methods. */
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <mach/vm_param.h>
#include <vm/pmap.h>
#include "i9100_dma_native.h"
static uint8_t dma_model_memory[32768] __attribute__((aligned(4096)));
static uint32_t dma_model_ptes[8],dma_model_linear[8];
static unsigned dma_model_next,dma_model_descriptors,dma_model_prepared,dma_model_bad_alias,dma_model_barriers;
void OSMemoryBarrier(){dma_model_barriers++;}
IOBufferMemoryDescriptor *IOBufferMemoryDescriptor::withOptions(unsigned options,size_t bytes,size_t alignment)
{
 if(fail())return nullptr;assert(options==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapInhibitCache));
 assert(alignment==4096U&&bytes&&!(bytes&4095U)&&dma_model_next+bytes<=sizeof(dma_model_memory));
 auto *d=new IOBufferMemoryDescriptor;d->data=dma_model_memory+dma_model_next;d->pa=0x46000000U+dma_model_next;
 d->bytes=bytes;d->prepared=0;dma_model_next+=(unsigned)bytes;dma_model_descriptors++;return d;
}
IOReturn IOBufferMemoryDescriptor::prepare(unsigned direction)
{assert(direction==kIODirectionInOut&&!prepared);if(fail())return -1;prepared=1;dma_model_prepared++;return 0;}
IOReturn IOBufferMemoryDescriptor::complete(unsigned direction)
{assert(direction==kIODirectionInOut&&prepared&&dma_model_prepared);prepared=0;dma_model_prepared--;return 0;}
addr64_t IOBufferMemoryDescriptor::getPhysicalSegment(unsigned offset,IOByteCount *span)
{assert(!offset&&prepared);*span=bytes;return pa;}
void *IOBufferMemoryDescriptor::getBytesNoCopy(){return data;}
void IOBufferMemoryDescriptor::release(){assert(!prepared&&dma_model_descriptors);dma_model_descriptors--;delete this;}
extern "C" {
unsigned long gPhysBase=0x40000000U,gVirtBase=0x80000000U;
pmap_t kernel_pmap=(void *)1;
void pmap_set_cache_attributes(ppnum_t page,unsigned flags)
{
 assert(flags==(VM_MEM_COHERENT|VM_MEM_NOT_CACHEABLE|VM_MEM_GUARDED));
 unsigned index=page-(0x46000000U>>12);assert(index<8);
 dma_model_ptes[index]=(page<<12)|2U|0x40U;dma_model_linear[index]=(page<<12)|2U|(dma_model_bad_alias?0U:0x40U);
}
vm_offset_t pmap_pte(pmap_t pmap,vm_offset_t va)
{
 assert(pmap==kernel_pmap);uintptr_t start=(uintptr_t)dma_model_memory;
 if(va>=start&&va<start+sizeof(dma_model_memory))return (uintptr_t)&dma_model_ptes[(va-start)/4096U];
 if(va>=0x86000000U&&va<0x86008000U)return (uintptr_t)&dma_model_linear[(va-0x86000000U)/4096U];return 0;
}
}
