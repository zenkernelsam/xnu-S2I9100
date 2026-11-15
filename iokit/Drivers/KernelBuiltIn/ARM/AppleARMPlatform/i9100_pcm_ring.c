/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_pcm_ring.h"
#include <string.h>
static int valid(struct i9100_pcm_ring *r)
{
 if(!r||!r->storage||!r->capacity)return I9100_RING_ARGUMENT;
 if(r->self!=(uintptr_t)r)return I9100_RING_OWNER;
 if(r->capacity>32768U||r->frames>r->capacity||r->read_index>=r->capacity||r->write_index>=r->capacity)
  return I9100_RING_OWNER;
 return 0;
}
static int alias(struct i9100_pcm_ring *r,const int16_t *p,unsigned frames)
{
 uintptr_t start=(uintptr_t)p,base=(uintptr_t)r->storage;uint32_t bytes=frames*4U,owned=r->capacity*4U;
 if(frames&&(!p||(start&1U)||start>UINTPTR_MAX-bytes))return I9100_RING_ARGUMENT;
 return frames&&start<base+owned&&base<start+bytes?I9100_RING_OWNER:0;
}
int i9100_pcm_ring_init(struct i9100_pcm_ring *r,int16_t *storage,uint32_t capacity)
{
 if(!r||r->storage||!storage||((uintptr_t)storage&1U)||!capacity||capacity>32768U||
    (uintptr_t)storage>UINTPTR_MAX-capacity*4U)return I9100_RING_ARGUMENT;
 memset(r,0,sizeof(*r));r->storage=storage;r->self=(uintptr_t)r;r->capacity=capacity;return 0;
}
int i9100_pcm_ring_write(struct i9100_pcm_ring *r,const int16_t *samples,uint32_t frames)
{
 int e=valid(r);if(e)return e;if(frames>r->capacity)return I9100_RING_ARGUMENT;
 if(frames>r->capacity-r->frames)return I9100_RING_FULL;e=alias(r,samples,frames);if(e)return e;
 if(r->written>UINT64_MAX-frames)return I9100_RING_OVERFLOW;
 uint32_t first=frames<r->capacity-r->write_index?frames:r->capacity-r->write_index;
 if(first)memcpy(r->storage+r->write_index*2U,samples,first*4U);
 if(frames>first)memcpy(r->storage,samples+first*2U,(frames-first)*4U);
 r->write_index=(r->write_index+frames)%r->capacity;r->frames+=frames;r->written+=frames;return 0;
}
int i9100_pcm_ring_peek(struct i9100_pcm_ring *r,int16_t *destination,uint32_t frames)
{
 int e=valid(r);if(e)return e;if(frames>r->frames)return I9100_RING_ARGUMENT;e=alias(r,destination,frames);if(e)return e;
 uint32_t first=frames<r->capacity-r->read_index?frames:r->capacity-r->read_index;
 if(first)memcpy(destination,r->storage+r->read_index*2U,first*4U);
 if(frames>first)memcpy(destination+first*2U,r->storage,(frames-first)*4U);return 0;
}
int i9100_pcm_ring_consume(struct i9100_pcm_ring *r,uint32_t frames)
{
 int e=valid(r);if(e)return e;if(frames>r->frames)return I9100_RING_ARGUMENT;
 if(r->consumed>UINT64_MAX-frames)return I9100_RING_OVERFLOW;
 r->read_index=(r->read_index+frames)%r->capacity;r->frames-=frames;r->consumed+=frames;return 0;
}
