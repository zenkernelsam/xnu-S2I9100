/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_pl330_ring.h"
#include "i9100_i2s.h"
#include <string.h>
enum {RI_INTEN=0x20,RI_ES=0x24,RI_INTCLR=0x2c,RI_FSM=0x30,RI_FSC=0x34,
 RI_CS=0x100,RI_CPC=0x104,RI_DBGSTATUS=0xd00,RI_DBGCMD=0xd04,RI_DBGINST0=0xd08,RI_DBGINST1=0xd0c};
#define RING_IRQ (1U<<3)
#define RING_GATE (1U<<4)
struct ring_encoder {uint8_t code[128];uint32_t used;};
static void rb(struct ring_encoder *e,uint8_t value){e->code[e->used++]=value;}
static void rp(struct ring_encoder *e,uint8_t op,uint8_t value){rb(e,op);rb(e,value);}
static void rm(struct ring_encoder *e,uint8_t reg,uint32_t value)
{rp(e,0xbc,reg);for(unsigned n=0;n<4;n++)rb(e,(uint8_t)(value>>(8U*n)));}
static void word_loop(struct ring_encoder *e,uint32_t words)
{
 rp(e,0x20,(uint8_t)(words-1U));uint32_t start=e->used;
 rp(e,0x32,12U<<3);rb(e,4);rp(e,0x2b,12U<<3);rp(e,0x38,(uint8_t)(e->used-start));
}
int i9100_pl330_ring_encode(uint32_t a,uint32_t b,uint32_t frames,uint8_t *code,
 uint32_t capacity,uint32_t *used,uint32_t offsets[2])
{
 if(used)*used=0;
 if(!code||!used||!offsets||capacity<128U||capacity>4096U||frames<16U||frames>512U||
    (a&63U)||(b&63U)||a<0x40000000U||b<0x40000000U||
    (uint64_t)a+frames*4U>0x80000000ULL||(uint64_t)b+frames*4U>0x80000000ULL||
    ((uint64_t)a<b+frames*4ULL&&(uint64_t)b<a+frames*4ULL))return I9100_RING_ARGUMENT;
 struct ring_encoder e={{0},0};
 uint32_t ccr=1U|(2U<<1)|(2U<<15)|(1U<<9)|(1U<<23);
 rm(&e,1,ccr);rm(&e,2,0x03830010U);rp(&e,0x35,12U<<3);
 uint32_t loop=e.used;
 for(unsigned slot=0;slot<2;slot++){
  rm(&e,0,slot?b:a);
  uint32_t remaining=frames;
  while(remaining){uint32_t n=remaining>256U?256U:remaining;word_loop(&e,n);remaining-=n;}
  rb(&e,0x13);rp(&e,0x34,3U<<3);offsets[slot]=e.used;rp(&e,0x36,4U<<3);
 }
 uint32_t distance=e.used-loop;if(!distance||distance>255U||e.used+2U>sizeof(e.code))return I9100_RING_ARGUMENT;
 rp(&e,0x28,(uint8_t)distance);memcpy(code,e.code,e.used);*used=e.used;return 0;
}
static int valid(struct i9100_pl330_ring *r)
{return !r||!r->initialized?I9100_RING_ARGUMENT:r->self!=(uintptr_t)r?I9100_RING_OWNER:0;}
static uint32_t rr(struct i9100_pl330_ring *r,unsigned off){return r->io.registers.read32(r->io.registers.cookie,off);}
static void rw(struct i9100_pl330_ring *r,unsigned off,uint32_t value){r->io.registers.write32(r->io.registers.cookie,off,value);}
static int enter(struct i9100_pl330_ring *r){return r->io.registers.lock(r->io.registers.cookie)?I9100_RING_BUSY:0;}
static void leave(struct i9100_pl330_ring *r){r->io.registers.unlock(r->io.registers.cookie);}
static uint64_t rn(struct i9100_pl330_ring *r){return r->io.registers.now_ns(r->io.registers.cookie);}
static int fault(struct i9100_pl330_ring *r,int error)
{if(!r->fault)r->error=error;r->fault=1;return error;}
static int stopped(struct i9100_pl330_ring *r)
{return !(rr(r,RI_CS)&15U)&&!(rr(r,RI_DBGSTATUS)&1U)&&!rr(r,RI_FSM)&&!rr(r,RI_FSC);}
static int foreign(struct i9100_pl330_ring *r)
{
 unsigned count=((rr(r,0xe00)>>4)&7U)+1U;
 for(unsigned n=1;n<count;n++)if(rr(r,RI_CS+n*8U)&15U)return 1;
 return 0;
}
static int fence(struct i9100_pl330_ring *r,unsigned slot,int irq_required)
{
 if(slot>1U)return 0;
 uint32_t a=rr(r,RI_CS)&0x1ffU,pc=rr(r,RI_CPC),events=rr(r,RI_ES);
 uint32_t b=rr(r,RI_CS)&0x1ffU,end_pc=rr(r,RI_CPC),ints=rr(r,RI_INTEN);
 return a==0x44U&&b==0x44U&&pc==end_pc&&pc==r->pa[2]+r->gate_offset[slot]&&
  !(events&RING_GATE)&&(!irq_required||(events&RING_IRQ))&&
  ints==RING_IRQ&&!foreign(r)&&!(rr(r,RI_DBGSTATUS)&1U)&&!rr(r,RI_FSM)&&!rr(r,RI_FSC);
}
int i9100_pl330_ring_buffer_idle(void *cookie)
{
 struct i9100_pl330_ring_buffer_cookie *c=cookie;
 if(!c||c->self!=(uintptr_t)c||c->slot>3U||valid(c->ring))return -1;
 struct i9100_pl330_ring *r=c->ring;
 if(stopped(r))return 0;
 return r->active&&r->phase==I9100_RING_FENCED&&r->pending&&c->slot==r->pending_slot&&
  fence(r,c->slot,0)?0:-1;
}
int i9100_pl330_ring_buffer_cookie_init(struct i9100_pl330_ring_buffer_cookie *c,struct i9100_pl330_ring *r,unsigned slot)
{
 int e=valid(r);if(e)return e;if(!c||c->self||slot>3U)return I9100_RING_ARGUMENT;
 c->ring=r;c->self=(uintptr_t)c;c->slot=slot;return 0;
}
int i9100_pl330_ring_init(struct i9100_pl330_ring *r,const struct i9100_pl330_ring_io *io)
{
 if(!r||r->initialized||!io||!io->registers.read32||!io->registers.write32||!io->registers.lock||
    !io->registers.unlock||!io->registers.now_ns||!io->registers.delay_us||!io->quiesce_requests)return I9100_RING_ARGUMENT;
 memset(r,0,sizeof(*r));r->io=*io;r->self=(uintptr_t)r;r->initialized=1;r->phase=I9100_RING_IDLE;return 0;
}
static uint32_t ident(struct i9100_pl330_ring *r,unsigned off)
{uint32_t id=0;for(unsigned n=0;n<4;n++)id|=(rr(r,off+4U*n)&255U)<<(8U*n);return id;}
static int wait_debug(struct i9100_pl330_ring *r)
{
 uint64_t start=rn(r);
 for(unsigned n=0;n<2500U;n++){
  if(!(rr(r,RI_DBGSTATUS)&1U))return 0;uint64_t now=rn(r);
  if(now<start||now-start>5000000ULL)return I9100_RING_TIMEOUT;
  if(r->io.registers.delay_us(r->io.registers.cookie,2))return I9100_RING_IO;
 }return I9100_RING_TIMEOUT;
}
static int debug(struct i9100_pl330_ring *r,unsigned op,uint32_t argument)
{
 int e=wait_debug(r);if(e)return e;
 uint32_t ins=op==1U?(1U<<16)|1U:op==0x34U?(0x34U<<16)|(4U<<27):(0xa2U<<16);
 uint32_t value=op==0xa2U?argument:0;
 rw(r,RI_DBGINST0,ins);rw(r,RI_DBGINST1,value);
 rw(r,RI_DBGCMD,0);
 return wait_debug(r);
}
int i9100_pl330_ring_enable(struct i9100_pl330_ring *r,int enabled)
{
 int e=valid(r);if(e)return e;if(enabled!=0&&enabled!=1)return I9100_RING_ARGUMENT;if((e=enter(r)))return e;
 if(enabled){
  if(r->enabled||r->owned||r->active||r->fault){e=I9100_RING_BUSY;goto done;}
  uint32_t config=rr(r,0xe00);unsigned channels=((config>>4)&7U)+1U;
  if((ident(r,0xfe0)&0xfffffU)!=0x41330U||ident(r,0xff0)!=0xb105f00dU||
     ((config>>17)&31U)+1U<=4U||!(config&1U)||((config>>12)&31U)+1U<=12U||
     !(rr(r,0xe0c)&(1U<<12))||(rr(r,0xe10)&(RING_IRQ|RING_GATE))!=(RING_IRQ|RING_GATE))
   {e=I9100_RING_ARGUMENT;goto done;}
  if(!stopped(r)||rr(r,RI_INTEN)||(rr(r,RI_ES)&(RING_IRQ|RING_GATE))){e=I9100_RING_BUSY;goto done;}
  for(unsigned n=1;n<channels;n++)if(rr(r,RI_CS+n*8U)&15U){e=I9100_RING_BUSY;goto done;}
  r->saved_inten=rr(r,RI_INTEN)&(RING_IRQ|RING_GATE);r->owned=1;
  rw(r,RI_INTEN,(rr(r,RI_INTEN)&~RING_GATE)|RING_IRQ);
  if((rr(r,RI_INTEN)&(RING_IRQ|RING_GATE))!=RING_IRQ){e=fault(r,I9100_RING_IO);goto done;}
  r->enabled=1;
 }else{
  if(!r->owned){e=I9100_RING_DISABLED;goto done;}
  if(r->active||r->held_mask||!stopped(r)||(rr(r,RI_ES)&(RING_IRQ|RING_GATE))){e=I9100_RING_BUSY;goto done;}
  rw(r,RI_INTEN,(rr(r,RI_INTEN)&~(RING_IRQ|RING_GATE))|r->saved_inten);
  if((rr(r,RI_INTEN)&(RING_IRQ|RING_GATE))!=r->saved_inten){e=fault(r,I9100_RING_IO);goto done;}
  r->enabled=r->owned=r->fault=0;
 }
done:leave(r);return e;
}
static int cpu_buffer(struct i9100_dma_buffer *b,uint32_t needed)
{
 return b&&b->self==(uintptr_t)b&&b->state==I9100_DMA_CPU&&b->cpu&&!(b->cpu&63U)&&
  !(b->physical&63U)&&!(b->bytes&63U)&&b->bytes>=needed&&b->physical>=0x40000000U&&
  (uint64_t)b->physical+b->bytes<=0x80000000ULL&&b->cpu<=UINTPTR_MAX-b->bytes;
}
static int identity(struct i9100_pl330_ring *r,unsigned n)
{
 struct i9100_dma_buffer *b=r->buffers[n];
 return b&&b->self==(uintptr_t)b&&b->cpu==r->cpu[n]&&b->physical==r->pa[n]&&b->bytes==r->bytes[n]&&
  b->holds&&b->state!=I9100_DMA_RELEASED;
}
static int program_valid(struct i9100_pl330_ring *r)
{
 uint8_t expected[128];uint32_t used=0,offsets[2];
 if(!identity(r,2)||i9100_pl330_ring_encode(r->pa[0],r->pa[1],r->period_frames,expected,128,&used,offsets)||
    used!=r->program_bytes||offsets[0]!=r->gate_offset[0]||offsets[1]!=r->gate_offset[1])return 0;
 return !memcmp((const void *)r->cpu[2],expected,used);
}
static int alias(struct i9100_dma_buffer *a,struct i9100_dma_buffer *b)
{
 return a==b||(a->cpu<b->cpu+b->bytes&&b->cpu<a->cpu+a->bytes)||
  (a->physical<(uint64_t)b->physical+b->bytes&&b->physical<(uint64_t)a->physical+a->bytes);
}
static int unpin(struct i9100_pl330_ring *r)
{
 int error=0;
 for(unsigned n=0;n<4;n++)if(r->held_mask&(1U<<n)){
  if(i9100_dma_unhold(r->buffers[n]))error=I9100_RING_IO;
  else r->held_mask&=~(1U<<n);
 }
 return error;
}
int i9100_pl330_ring_prepare(struct i9100_pl330_ring *r,struct i9100_dma_buffer *a,
 struct i9100_dma_buffer *b,struct i9100_dma_buffer *code,struct i9100_dma_buffer *drain,
 uint32_t rate,uint32_t frames)
{
 int e=valid(r);if(e)return e;
 if((rate!=44100U&&rate!=48000U)||frames<16U||frames>512U)return I9100_RING_ARGUMENT;
 if((e=enter(r)))return e;
 if(!r->enabled){e=I9100_RING_DISABLED;goto done;}
 if(r->active||r->held_mask||r->fault||!stopped(r)||foreign(r)||(rr(r,RI_ES)&(RING_IRQ|RING_GATE))||
    rr(r,RI_INTEN)!=RING_IRQ){e=I9100_RING_BUSY;goto done;}
 struct i9100_dma_buffer *all[4]={a,b,code,drain};
 for(unsigned n=0;n<4;n++)if(!cpu_buffer(all[n],n<2U?frames*4U:128U)||all[n]->holds){e=I9100_RING_ARGUMENT;goto done;}
 for(unsigned n=0;n<4;n++)for(unsigned k=n+1;k<4;k++)if(alias(all[n],all[k])){e=I9100_RING_ARGUMENT;goto done;}
 for(unsigned n=0;n<4;n++){
  r->buffers[n]=all[n];r->cpu[n]=all[n]->cpu;r->pa[n]=all[n]->physical;r->bytes[n]=all[n]->bytes;
  e=i9100_dma_hold(all[n]);if(e){r->cleanup_error=unpin(r);e=fault(r,I9100_RING_IO);goto done;}r->held_mask|=1U<<n;
 }
 e=i9100_pl330_ring_encode(a->physical,b->physical,frames,(uint8_t *)code->cpu,
  code->bytes>4096U?4096U:code->bytes,&r->program_bytes,r->gate_offset);if(e){e=fault(r,e);goto done;}
 const uint8_t drain_code[3]={0x36,4U<<3,0};memcpy((void *)drain->cpu,drain_code,sizeof(drain_code));
 memset((void *)a->cpu,0,frames*4U);memset((void *)b->cpu,0,frames*4U);
 r->sample_rate=rate;r->period_frames=frames;r->timeout_ms=(frames*1000U+rate-1U)/rate+50U;
 r->phase=I9100_RING_PREPARED;r->expected_slot=r->pending=r->queued=0;
 r->pending_slot=2;r->peak[0]=r->peak[1]=0;r->completed_frames=r->completed_periods=r->completed_silence_periods=0;
 r->start_ns=r->fence_ns=r->last_release_ns=0;r->error=r->cleanup_error=0;
done:leave(r);return e;
}
static int wait_go(struct i9100_pl330_ring *r,uint32_t pa,uint32_t bytes)
{
 uint64_t start=rn(r);
 for(unsigned n=0;n<2500U;n++){
  uint32_t state=rr(r,RI_CS)&15U,pc=rr(r,RI_CPC);
  if(rr(r,RI_FSM)||rr(r,RI_FSC))return I9100_RING_FAULT;
  if(state&&pc>=pa&&(uint64_t)pc<=(uint64_t)pa+bytes)return 0;
  uint64_t now=rn(r);if(now<start||now-start>5000000ULL)return I9100_RING_TIMEOUT;
  if(r->io.registers.delay_us(r->io.registers.cookie,2))return I9100_RING_IO;
 }return I9100_RING_TIMEOUT;
}
int i9100_pl330_ring_start(struct i9100_pl330_ring *r)
{
 int e=valid(r);if(e)return e;if((e=enter(r)))return e;
 if(!r->enabled){e=I9100_RING_DISABLED;goto done;}
 if(r->active||r->fault||r->phase!=I9100_RING_PREPARED||r->held_mask!=15U||!stopped(r)||foreign(r)||
    (rr(r,RI_ES)&(RING_IRQ|RING_GATE))||rr(r,RI_INTEN)!=RING_IRQ){e=I9100_RING_BUSY;goto done;}
 for(unsigned n=0;n<4;n++)if(!identity(r,n)||!cpu_buffer(r->buffers[n],n<2U?r->period_frames*4U:128U))
  {e=fault(r,I9100_RING_OWNER);goto done;}
 /* Initial silence is part of the driver, not a caller assertion. */
 for(unsigned n=0;n<2;n++)for(unsigned word=0;word<r->period_frames*2U;word++)
  if(((int16_t *)r->cpu[n])[word]){e=fault(r,I9100_RING_OWNER);goto done;}
 /* Never execute caller-modified bytes retained between prepare/start. */
 e=i9100_pl330_ring_encode(r->pa[0],r->pa[1],r->period_frames,(uint8_t *)r->cpu[2],
  r->bytes[2]>4096U?4096U:r->bytes[2],&r->program_bytes,r->gate_offset);
 if(e){e=fault(r,e);goto done;}
 r->active=1;r->expected_slot=0;
 for(unsigned n=0;n<3;n++){e=i9100_dma_prepare(r->buffers[n],I9100_DMA_TO_DEVICE);if(e){e=fault(r,I9100_RING_IO);goto done;}}
 r->phase=I9100_RING_RUNNING;r->start_ns=r->last_release_ns=rn(r);
 e=debug(r,0xa2U,r->pa[2]);if(!e)e=wait_go(r,r->pa[2],r->program_bytes);if(e)e=fault(r,e);
done:leave(r);return e;
}
int i9100_pl330_ring_poll(struct i9100_pl330_ring *r,uint32_t *completion)
{
 if(completion)*completion=0;int e=valid(r);if(e)return e;if(!completion)return I9100_RING_ARGUMENT;if((e=enter(r)))return e;
 if(!r->enabled||!r->active){e=I9100_RING_DISABLED;goto done;}
 if(r->fault){e=I9100_RING_FAULT;goto done;}
 uint64_t now=rn(r),base=r->pending?r->fence_ns:r->last_release_ns;
 if(now<base||now-base>(uint64_t)r->timeout_ms*1000000ULL){e=fault(r,I9100_RING_TIMEOUT);goto done;}
 if(rr(r,RI_FSM)||rr(r,RI_FSC)||foreign(r)||rr(r,RI_INTEN)!=RING_IRQ)
  {e=fault(r,I9100_RING_FAULT);goto done;}
 if(r->pending){e=I9100_RING_BUSY;goto done;}
 unsigned slot=r->expected_slot;
 if(!fence(r,slot,1)){e=I9100_RING_BUSY;goto done;}
 for(unsigned n=0;n<4;n++)if(!identity(r,n)||r->buffers[n]->state!=(n==3U?I9100_DMA_CPU:I9100_DMA_DEVICE))
  {e=fault(r,I9100_RING_OWNER);goto done;}
 if(!program_valid(r)){e=fault(r,I9100_RING_OWNER);goto done;}
 uint32_t actual_peak=0;
 for(unsigned n=0;n<r->period_frames*2U;n++){
  int32_t value=((const int16_t *)r->cpu[slot])[n];uint32_t magnitude=(uint32_t)(value<0?-value:value);
  if(magnitude>actual_peak)actual_peak=magnitude;
 }
 if(actual_peak>1036U||actual_peak!=r->peak[slot]){e=fault(r,I9100_RING_OWNER);goto done;}
 if(r->completed_periods==UINT64_MAX||r->completed_frames>UINT64_MAX-r->period_frames||
    (!r->peak[slot]&&r->completed_silence_periods==UINT64_MAX)){e=fault(r,I9100_RING_OVERFLOW);goto done;}
 r->pending=1;r->pending_slot=slot;r->phase=I9100_RING_FENCED;
 e=i9100_dma_complete(r->buffers[slot]);if(e){e=fault(r,I9100_RING_IO);goto done;}
 rw(r,RI_INTCLR,RING_IRQ);if(rr(r,RI_ES)&RING_IRQ){e=fault(r,I9100_RING_IO);goto done;}
 r->completed_periods++;r->completed_frames+=r->period_frames;if(!r->peak[slot])r->completed_silence_periods++;
 r->fence_ns=now;r->queued=0;*completion=1;e=0;
done:leave(r);return e;
}
int i9100_pl330_ring_queue(struct i9100_pl330_ring *r,const int16_t *samples,uint32_t frames)
{
 int e=valid(r);if(e)return e;
 if(frames>r->period_frames||(frames&&(!samples||((uintptr_t)samples&1U)||
    (uintptr_t)samples>UINTPTR_MAX-frames*4U)))return I9100_RING_ARGUMENT;
 if((e=enter(r)))return e;
 if(!r->active||!r->pending||r->phase!=I9100_RING_FENCED){e=I9100_RING_BUSY;goto done;}
 if(r->fault){e=I9100_RING_FAULT;goto done;}
 if(r->queued){e=I9100_RING_BUSY;goto done;}
 unsigned slot=r->pending_slot;
 if(!fence(r,slot,0)||!identity(r,slot)||!cpu_buffer(r->buffers[slot],r->period_frames*4U))
  {e=fault(r,I9100_RING_OWNER);goto done;}
 if(frames)for(unsigned n=0;n<4;n++)if((uintptr_t)samples<r->cpu[n]+r->bytes[n]&&r->cpu[n]<(uintptr_t)samples+frames*4U)
  {e=I9100_RING_OWNER;goto done;}
 int16_t *target=(int16_t *)r->cpu[slot];memset(target,0,r->period_frames*4U);
 if(frames)i9100_pcm_attenuate_30db(target,samples,frames*2U);
 uint32_t peak=0;for(unsigned n=0;n<r->period_frames*2U;n++){int32_t v=target[n];uint32_t magnitude=(uint32_t)(v<0?-v:v);if(magnitude>peak)peak=magnitude;}
 if(peak>1036U||!fence(r,slot,0)){e=fault(r,I9100_RING_OWNER);goto done;}
 r->peak[slot]=peak;r->queued=1;e=0;
done:leave(r);return e;
}
int i9100_pl330_ring_release_period(struct i9100_pl330_ring *r)
{
 int e=valid(r);if(e)return e;if((e=enter(r)))return e;
 if(r->fault){e=I9100_RING_FAULT;goto done;}
 if(!r->active||!r->pending||r->phase!=I9100_RING_FENCED){e=I9100_RING_BUSY;goto done;}
 unsigned slot=r->pending_slot;uint64_t now=rn(r);
 if(now<r->fence_ns||now-r->fence_ns>(uint64_t)r->timeout_ms*1000000ULL){e=fault(r,I9100_RING_TIMEOUT);goto done;}
 if(!fence(r,slot,0)||(rr(r,RI_ES)&(RING_IRQ|RING_GATE))||!program_valid(r)||!identity(r,slot)||!cpu_buffer(r->buffers[slot],r->period_frames*4U))
  {e=fault(r,I9100_RING_OWNER);goto done;}
 if(!r->queued){memset((void *)r->cpu[slot],0,r->period_frames*4U);r->peak[slot]=0;}
 for(unsigned n=0;n<r->period_frames*2U;n++){
  int32_t value=((const int16_t *)r->cpu[slot])[n];if(value>1036||value<-1036){e=fault(r,I9100_RING_OWNER);goto done;}
 }
 e=i9100_dma_prepare(r->buffers[slot],I9100_DMA_TO_DEVICE);if(e){e=fault(r,I9100_RING_IO);goto done;}
 uint32_t old_pc=r->pa[2]+r->gate_offset[slot];
 r->pending=0;r->pending_slot=2;r->queued=0;r->expected_slot^=1U;r->phase=I9100_RING_RUNNING;r->last_release_ns=now;
 e=debug(r,0x34U,0);if(e){e=fault(r,e);goto done;}
 /* A posted gate or changed PC/state is evidence of release submission,
  * not another completion. An ignored debug command cannot look successful. */
 if(!(rr(r,RI_ES)&RING_GATE)&&(rr(r,RI_CS)&0x1ffU)==0x44U&&rr(r,RI_CPC)==old_pc)
  e=fault(r,I9100_RING_IO);
done:leave(r);return e;
}
static int wait_stop(struct i9100_pl330_ring *r)
{
 uint64_t start=rn(r);
 for(unsigned n=0;n<25000U;n++){
  if(!(rr(r,RI_CS)&15U)&&!(rr(r,RI_DBGSTATUS)&1U))return 0;
  uint64_t now=rn(r);if(now<start||now-start>50000000ULL)return I9100_RING_TIMEOUT;
  if(r->io.registers.delay_us(r->io.registers.cookie,2))return I9100_RING_IO;
 }return I9100_RING_TIMEOUT;
}
int i9100_pl330_ring_stop(struct i9100_pl330_ring *r)
{
 int e=valid(r);if(e)return e;if((e=enter(r)))return e;
 if(!r->active&&!r->held_mask){e=0;goto done;}
 e=r->io.quiesce_requests(r->io.requests_cookie);if(e){e=fault(r,I9100_RING_IO);goto done;}
 r->phase=I9100_RING_STOPPING;
 if(!stopped(r)){e=debug(r,1U,0);if(!e)e=wait_stop(r);if(e){e=fault(r,e);goto done;}}
 if(!stopped(r)){e=fault(r,I9100_RING_FAULT);goto done;}
 for(unsigned n=0;n<4;n++)if(r->held_mask&(1U<<n)){
  if(!identity(r,n)){e=fault(r,I9100_RING_OWNER);goto done;}
  struct i9100_dma_buffer *b=r->buffers[n];
  if(b->state==I9100_DMA_DEVICE||b->state==I9100_DMA_QUARANTINED){e=i9100_dma_abort(b);if(e){e=fault(r,I9100_RING_IO);goto done;}}
  else if(b->state!=I9100_DMA_CPU){e=fault(r,I9100_RING_OWNER);goto done;}
 }
 rw(r,RI_INTCLR,RING_IRQ);if(rr(r,RI_ES)&RING_IRQ){e=fault(r,I9100_RING_IO);goto done;}
 if(rr(r,RI_ES)&RING_GATE){
  /* INTCLR cannot clear a non-interrupt event. Consume it through a
   * different, immutable WFE+END allocation after the output is quiescent. */
  if(foreign(r)){e=fault(r,I9100_RING_OWNER);goto done;}
  const uint8_t drain_code[3]={0x36,4U<<3,0};memcpy((void *)r->cpu[3],drain_code,3);
  r->phase=I9100_RING_DRAINING;e=i9100_dma_prepare(r->buffers[3],I9100_DMA_TO_DEVICE);
  if(!e)e=debug(r,0xa2U,r->pa[3]);if(!e)e=wait_stop(r);
  if(e||!stopped(r)||(rr(r,RI_ES)&RING_GATE)){e=fault(r,e?e:I9100_RING_IO);goto done;}
  e=i9100_dma_complete(r->buffers[3]);if(e){e=fault(r,I9100_RING_IO);goto done;}
 }
 e=unpin(r);if(e){e=fault(r,e);goto done;}
 r->active=r->fault=r->pending=r->queued=0;r->pending_slot=2;r->phase=I9100_RING_IDLE;
done:r->cleanup_error=e;leave(r);return e;
}
int i9100_pl330_ring_status(struct i9100_pl330_ring *r,struct i9100_pl330_ring_status *out)
{
 int e=valid(r);if(e)return e;if(!out)return I9100_RING_ARGUMENT;if((e=enter(r)))return e;
 struct i9100_pl330_ring_status value={0};
 value.enabled=r->enabled;value.active=r->active;value.fault=r->fault;value.phase=r->phase;
 value.held_mask=r->held_mask;value.pending_slot=r->pending_slot;value.pending=r->pending;value.expected_slot=r->expected_slot;
 value.program_bytes=r->program_bytes;value.period_frames=r->period_frames;value.sample_rate=r->sample_rate;
 value.cs=rr(r,RI_CS);value.cpc=rr(r,RI_CPC);value.events=rr(r,RI_ES);value.interrupts=rr(r,RI_INTEN);
 value.manager_fault=rr(r,RI_FSM);value.channel_fault=rr(r,RI_FSC);
 value.completed_periods=r->completed_periods;value.completed_frames=r->completed_frames;
 value.completed_silence_periods=r->completed_silence_periods;value.start_ns=r->start_ns;value.fence_ns=r->fence_ns;
 value.error=r->error;value.cleanup_error=r->cleanup_error;*out=value;leave(r);return 0;
}
