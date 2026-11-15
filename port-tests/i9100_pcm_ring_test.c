/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_pcm_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int16_t storage[34],source[34],out[34];
int main(void)
{
 struct i9100_pcm_ring r={0};for(unsigned n=0;n<34;n++)source[n]=(int16_t)(n*1973U);
 assert(!i9100_pcm_ring_init(&r,storage,17));assert(!i9100_pcm_ring_write(&r,source,13));
 assert(!i9100_pcm_ring_peek(&r,out,13)&&!memcmp(source,out,52)&&r.frames==13&&!r.consumed);
 assert(!i9100_pcm_ring_consume(&r,11));assert(!i9100_pcm_ring_write(&r,source+6,12));
 assert(!i9100_pcm_ring_peek(&r,out,14)&&!memcmp(out,source+22,8)&&!memcmp(out+4,source+6,48));
 assert(i9100_pcm_ring_write(&r,source,4)==I9100_RING_FULL&&r.frames==14&&r.written==25);
 assert(!i9100_pcm_ring_consume(&r,14)&&!r.frames&&r.consumed==25);
 assert(i9100_pcm_ring_peek(&r,out,1)==I9100_RING_ARGUMENT);
 assert(i9100_pcm_ring_write(&r,storage,1)==I9100_RING_OWNER&&!r.frames);
 struct i9100_pcm_ring copy=r;assert(i9100_pcm_ring_write(&copy,source,1)==I9100_RING_OWNER);
 for(unsigned turn=0;turn<1000;turn++){
  assert(!i9100_pcm_ring_write(&r,source,17));assert(!i9100_pcm_ring_peek(&r,out,17));
  assert(!memcmp(out,source,68));assert(!i9100_pcm_ring_consume(&r,17));
 }
 r.written=UINT64_MAX;assert(i9100_pcm_ring_write(&r,source,1)==I9100_RING_OVERFLOW&&!r.frames);
 assert(!i9100_pcm_ring_write(&r,0,0));
 r.frames=18;assert(i9100_pcm_ring_write(&r,source,1)==I9100_RING_OWNER);r.frames=0;
 r.write_index=17;assert(i9100_pcm_ring_write(&r,source,1)==I9100_RING_OWNER);
 puts("PASS CPU stereo16 ring/nonpower-of-two wrap/atomic full reject/peek-before-commit/aliases/copy guard/1000 full turns/overflow");return 0;
}
