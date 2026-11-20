/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_dma_guard.h"
#include <string.h>
static int enter(struct i9100_dma_buffer *b){return b->io.lock(b->io.cookie)?I9100_DMA_BUSY:0;}
static void leave(struct i9100_dma_buffer *b){b->io.unlock(b->io.cookie);}
static int sync(struct i9100_dma_buffer *b,unsigned action){return b->io.sync(b->io.cookie,b->cpu,b->physical,b->bytes,action)?I9100_DMA_IO:0;}
int i9100_dma_bind(struct i9100_dma_buffer *b,const struct i9100_dma_io *io,uintptr_t cpu,uint32_t pa,uint32_t bytes)
{
 if(!b||!io||b->state||!io->lock||!io->unlock||!io->sync||!io->is_idle||!io->release||
    !cpu||(cpu&63U)||(pa&63U)||(bytes&63U)||!bytes||bytes>16U*1024U*1024U||
    pa<0x40000000U||(uint64_t)pa+bytes>0x80000000ULL||cpu>UINTPTR_MAX-bytes)return I9100_DMA_ARGUMENT;
 memset(b,0,sizeof(*b));b->io=*io;b->cpu=cpu;b->self=(uintptr_t)b;b->physical=pa;b->bytes=bytes;b->state=I9100_DMA_BOUND;return 0;
}
int i9100_dma_enable(struct i9100_dma_buffer *b,int enabled)
{
 if(!b||!b->state||(enabled!=0&&enabled!=1))return I9100_DMA_ARGUMENT;
 if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if((enabled&&b->state!=I9100_DMA_BOUND)||(!enabled&&b->state!=I9100_DMA_CPU)){leave(b);return I9100_DMA_OWNER;}
 if(b->io.is_idle(b->io.cookie)){leave(b);return I9100_DMA_BUSY;}
 b->state=enabled?I9100_DMA_CPU:I9100_DMA_BOUND;leave(b);return 0;
}
int i9100_dma_prepare(struct i9100_dma_buffer *b,enum i9100_dma_direction dir)
{
 if(!b||!b->state||dir<I9100_DMA_TO_DEVICE||dir>I9100_DMA_BIDIRECTIONAL)return I9100_DMA_ARGUMENT;
 if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if(b->state==I9100_DMA_BOUND){leave(b);return I9100_DMA_DISABLED;}
 if(b->state!=I9100_DMA_CPU){leave(b);return I9100_DMA_OWNER;}
 if(b->io.is_idle(b->io.cookie)){leave(b);return I9100_DMA_BUSY;}
 e=sync(b,dir==I9100_DMA_TO_DEVICE?I9100_DMA_CLEAN:I9100_DMA_CLEAN_INVALIDATE);
 if(!e)e=sync(b,I9100_DMA_BARRIER);
 if(e){b->state=I9100_DMA_QUARANTINED;leave(b);return e;}
 b->direction=(unsigned)dir;b->handed_to_device=1;b->state=I9100_DMA_DEVICE;leave(b);return 0;
}
static int take_back(struct i9100_dma_buffer *b)
{
 if(b->io.is_idle(b->io.cookie)){b->state=I9100_DMA_QUARANTINED;return I9100_DMA_BUSY;}
 int e=sync(b,I9100_DMA_BARRIER);
 if(!e&&!b->handed_to_device)e=sync(b,I9100_DMA_CLEAN);
 if(!e&&b->handed_to_device&&b->direction!=I9100_DMA_TO_DEVICE)e=sync(b,I9100_DMA_INVALIDATE);
 if(!e)e=sync(b,I9100_DMA_BARRIER);
 if(e){b->state=I9100_DMA_QUARANTINED;return e;}
 b->state=I9100_DMA_CPU;b->handed_to_device=0;return 0;
}
int i9100_dma_complete(struct i9100_dma_buffer *b)
{
 if(!b||!b->state)return I9100_DMA_ARGUMENT;
 if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if(b->state!=I9100_DMA_DEVICE){leave(b);return I9100_DMA_OWNER;}
 e=take_back(b);leave(b);return e;
}
int i9100_dma_abort(struct i9100_dma_buffer *b)
{
 if(!b||!b->state)return I9100_DMA_ARGUMENT;
 if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if(b->state!=I9100_DMA_DEVICE&&b->state!=I9100_DMA_QUARANTINED){leave(b);return I9100_DMA_OWNER;}
 e=take_back(b);leave(b);return e;
}
int i9100_dma_quarantine(struct i9100_dma_buffer *b)
{
 if(!b||!b->state)return I9100_DMA_ARGUMENT;if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if(b->state!=I9100_DMA_DEVICE&&b->state!=I9100_DMA_CPU&&b->state!=I9100_DMA_QUARANTINED)e=I9100_DMA_OWNER;
 else b->state=I9100_DMA_QUARANTINED;
 leave(b);return e;
}
int i9100_dma_release(struct i9100_dma_buffer *b)
{
 if(!b||!b->state)return I9100_DMA_ARGUMENT;
 if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if(b->holds){leave(b);return I9100_DMA_BUSY;}
 if(b->state!=I9100_DMA_CPU&&b->state!=I9100_DMA_BOUND){leave(b);return I9100_DMA_OWNER;}
 if(b->io.is_idle(b->io.cookie)){b->state=I9100_DMA_QUARANTINED;leave(b);return I9100_DMA_BUSY;}
 e=sync(b,I9100_DMA_CLEAN_INVALIDATE);if(!e)e=sync(b,I9100_DMA_BARRIER);
 if(e){b->state=I9100_DMA_QUARANTINED;leave(b);return e;}
 b->state=I9100_DMA_RELEASED;leave(b);b->io.release(b->io.cookie,b->cpu,b->physical,b->bytes);return 0;
}
int i9100_dma_hold(struct i9100_dma_buffer *b)
{
 if(!b||!b->state)return I9100_DMA_ARGUMENT;
 if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if(b->state==I9100_DMA_RELEASED||b->holds==UINT32_MAX)e=I9100_DMA_OWNER;
 else b->holds++;
 leave(b);return e;
}
int i9100_dma_unhold(struct i9100_dma_buffer *b)
{
 if(!b||!b->state)return I9100_DMA_ARGUMENT;
 if(b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 int e=enter(b);if(e)return e;
 if(!b->holds||b->state==I9100_DMA_RELEASED)e=I9100_DMA_OWNER;
 else b->holds--;
 leave(b);return e;
}
static int pair_hold(struct i9100_dma_buffer *a,struct i9100_dma_buffer *b,int acquire)
{
 if(!a||!b||a==b||!a->state||!b->state)return I9100_DMA_ARGUMENT;
 if(a->self!=(uintptr_t)a||b->self!=(uintptr_t)b)return I9100_DMA_OWNER;
 struct i9100_dma_buffer *first=(uintptr_t)a<(uintptr_t)b?a:b,*second=first==a?b:a;
 int shared=first->io.cookie==second->io.cookie&&first->io.lock==second->io.lock&&first->io.unlock==second->io.unlock;
 int e=enter(first);if(e)return e;
 if(!shared){e=enter(second);if(e){leave(first);return e;}}
 if(a->self!=(uintptr_t)a||b->self!=(uintptr_t)b||a->state!=I9100_DMA_CPU||b->state!=I9100_DMA_CPU||
    a->handed_to_device||b->handed_to_device||a->holds!=(acquire?0U:1U)||b->holds!=(acquire?0U:1U))e=I9100_DMA_OWNER;
 else a->holds=b->holds=acquire?1U:0U;
 if(!shared)leave(second);leave(first);return e;
}
int i9100_dma_hold_pair(struct i9100_dma_buffer *a,struct i9100_dma_buffer *b)
{return pair_hold(a,b,1);}
int i9100_dma_unhold_pair(struct i9100_dma_buffer *a,struct i9100_dma_buffer *b)
{return pair_hold(a,b,0);}
