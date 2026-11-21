/* SPDX-License-Identifier: BSD-2-Clause
 * Actual allocator linked; only IOKit memory/PTE/DSB are host facades. */
#include <IOKit/IOBufferMemoryDescriptor.h>
void OSMemoryBarrier(){m.syncs++;}
IOBufferMemoryDescriptor *IOBufferMemoryDescriptor::withOptions(unsigned options,size_t bytes,size_t alignment)
{
 if(fail())return nullptr;assert(options==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapInhibitCache));
 assert(alignment==131072U&&bytes&&!(bytes&4095U)&&bytes<=8388608U);
 unsigned k=dma_slots[0]?1U:0U;assert(!dma_slots[k]);dma_slots[k]=true;m.dma++;
 auto *d=new IOBufferMemoryDescriptor;d->data=k?bank_b:bank_a;d->pa=k?0x48000000U:0x46000000U;d->bytes=bytes;d->prepared=0;return d;
}
IOReturn IOBufferMemoryDescriptor::prepare(unsigned direction)
{assert(direction==kIODirectionInOut&&!prepared);if(fail())return -1;prepared=1;return 0;}
IOReturn IOBufferMemoryDescriptor::complete(unsigned direction)
{assert(direction==kIODirectionInOut&&prepared);prepared=0;return 0;}
addr64_t IOBufferMemoryDescriptor::getPhysicalSegment(unsigned offset,IOByteCount *span)
{assert(!offset&&prepared);*span=bytes;return pa;}
void *IOBufferMemoryDescriptor::getBytesNoCopy(){return data;}
void IOBufferMemoryDescriptor::release()
{assert(!prepared&&m.dma);unsigned k=data==bank_a?0U:1U;assert(dma_slots[k]);dma_slots[k]=false;m.dma--;delete this;}
