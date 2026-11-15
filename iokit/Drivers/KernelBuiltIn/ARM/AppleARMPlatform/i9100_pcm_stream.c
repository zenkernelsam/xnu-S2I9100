/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_pcm_stream.h"
#include <string.h>

static int valid(struct i9100_pcm_stream *s)
{return !s||!s->initialized?I9100_PCM_ARGUMENT:s->self!=(uintptr_t)s?I9100_PCM_OWNER:0;}
static struct i9100_dma_buffer *buffer(struct i9100_pcm_stream *s,unsigned index)
{return index<2U?s->period[index]:s->code;}
static int cpu_buffer(struct i9100_dma_buffer *b,unsigned bytes)
{return b&&b->self==(uintptr_t)b&&b->state==I9100_DMA_CPU&&b->cpu&&
 !(b->cpu&63U)&&!(b->physical&63U)&&b->bytes>=bytes&&!(b->bytes&63U)&&
 b->physical>=0x40000000U&&(uint64_t)b->physical+b->bytes<=0x80000000ULL&&
 b->cpu<=UINTPTR_MAX-b->bytes;}
static int overlap(uintptr_t a,uint32_t an,uintptr_t b,uint32_t bn)
{return a<=UINTPTR_MAX-an&&b<=UINTPTR_MAX-bn&&a<b+bn&&b<a+an;}
static int private_buffers(struct i9100_pcm_stream *s,unsigned bytes)
{
 for(unsigned n=0;n<3;n++)if(!cpu_buffer(buffer(s,n),n==2U?64U:bytes)||buffer(s,n)->holds)
  return I9100_PCM_OWNER;
 for(unsigned a=0;a<3;a++)for(unsigned b=a+1;b<3;b++){
  struct i9100_dma_buffer *x=buffer(s,a),*y=buffer(s,b);
  if(overlap(x->cpu,x->bytes,y->cpu,y->bytes)||
     overlap(x->physical,x->bytes,y->physical,y->bytes))return I9100_PCM_OWNER;
 }
 return 0;
}
static int our_job(struct i9100_pcm_stream *s)
{
 if(!s->dma->active)return 1;
 return s->in_flight<2U&&s->dma->job.mode==I9100_PL330_I2S_TX&&
  s->dma->job.source==s->period[s->in_flight]&&s->dma->job.code==s->code&&
  !s->dma->job.destination&&!s->dma->job.source_offset&&
  s->dma->job.bytes==s->period_bytes;
}
static int unpin(struct i9100_pcm_stream *s)
{
 int e=0;
 for(unsigned n=0;n<3;n++)if(s->held_mask&(1U<<n)){
  if(i9100_dma_unhold(buffer(s,n)))e=I9100_PCM_IO;
  else s->held_mask&=~(1U<<n);
 }
 return e;
}
static int cleanup(struct i9100_pcm_stream *s,int muted)
{
 if(!s->held_mask&&!s->i2s_owned&&!s->started){s->prepared=0;return 0;}
 s->ready_mask=0;
 /* An ownership violation must never kill an unrelated channel job. */
 if(!our_job(s))return I9100_PCM_OWNER;
 int mute_error=muted?0:s->io.ensure_muted(s->io.cookie);
 int e=0;
 if(s->i2s_owned){e=i9100_i2s_quiesce(s->i2s);if(e)return e;}
 e=i9100_pl330_stop(s->dma);if(e)return e;
 if(s->i2s_owned){e=i9100_i2s_stop(s->i2s);if(e)return e;s->i2s_owned=0;}
 if(s->dma->active||s->dma->fault||i9100_pl330_idle(s->dma))return I9100_PCM_IO;
 for(unsigned n=0;n<3;n++)if((s->held_mask&(1U<<n))&&!cpu_buffer(buffer(s,n),n==2U?64U:s->period_bytes))
  return I9100_PCM_OWNER;
 s->in_flight=2;
 if(mute_error)return mute_error;
 e=unpin(s);
 if(!e){s->prepared=s->started=0;s->next_index=0;}
 return e;
}
static int fail(struct i9100_pcm_stream *s,int error,int safe_mute)
{
 s->fault=1;s->last_error=error;s->cleanup_error=cleanup(s,safe_mute);
 return error==I9100_PCM_OWNER?I9100_PCM_OWNER:I9100_PCM_IO;
}
static int mute_fault(struct i9100_pcm_stream *s,int error)
{
 /* A failed mute may have applied only part of its I2C sequence. Preserve
  * the board/buffer lifetime until an explicit stop verifies silence. */
 s->fault=1;s->last_error=error;s->cleanup_error=0;return I9100_PCM_IO;
}
int i9100_pcm_stream_init(struct i9100_pcm_stream *s,const struct i9100_pcm_io *io,
 struct i9100_pl330 *dma,struct i9100_i2s *i2s,struct i9100_dma_buffer *p0,
 struct i9100_dma_buffer *p1,struct i9100_dma_buffer *code)
{
 if(!s||s->initialized||!io||!io->lock||!io->unlock||!io->ensure_muted||
    !dma||!dma->initialized||!i2s||!i2s->initialized||!p0||!p1||!code||
    p0==p1||p0==code||p1==code)return I9100_PCM_ARGUMENT;
 if((io->cookie==dma->io.cookie&&io->lock==dma->io.lock)||
    (io->cookie==i2s->io.cookie&&io->lock==i2s->io.lock)||
    (dma->io.cookie==i2s->io.cookie&&dma->io.lock==i2s->io.lock))return I9100_PCM_ARGUMENT;
 struct i9100_dma_buffer *all[3]={p0,p1,code};
 for(unsigned n=0;n<3;n++)if(all[n]->io.cookie==io->cookie&&all[n]->io.lock==io->lock)
  return I9100_PCM_ARGUMENT;
 memset(s,0,sizeof(*s));s->io=*io;s->dma=dma;s->i2s=i2s;
 s->period[0]=p0;s->period[1]=p1;s->code=code;s->self=(uintptr_t)s;
 s->initialized=1;s->in_flight=2;return 0;
}
int i9100_pcm_stream_enable(struct i9100_pcm_stream *s,int enabled)
{
 int e=valid(s);if(e)return e;if(enabled!=0&&enabled!=1)return I9100_PCM_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_PCM_BUSY;
 e=s->prepared||s->started||s->held_mask||s->i2s_owned||s->fault?I9100_PCM_BUSY:0;
 if(!e)s->enabled=enabled;s->io.unlock(s->io.cookie);return e;
}
int i9100_pcm_stream_prepare(struct i9100_pcm_stream *s,uint32_t rate,
 uint32_t frames,uint32_t bclk,int verified)
{
 int e=valid(s);if(e)return e;
 if((rate!=44100U&&rate!=48000U)||frames<16U||frames>4096U||bclk!=rate*32U||verified!=1)
  return I9100_PCM_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_PCM_BUSY;
 if(!s->enabled){e=I9100_PCM_DISABLED;goto done;}
 if(s->prepared||s->started||s->held_mask||s->fault||!s->dma->enabled||s->dma->active||
    s->dma->fault||!s->i2s->enabled||s->i2s->prepared||s->i2s->fault||i9100_pl330_idle(s->dma))
  {e=I9100_PCM_BUSY;goto done;}
 e=private_buffers(s,frames*4U);if(e)goto done;
 s->sample_rate=rate;s->period_frames=frames;s->period_bytes=frames*4U;
 s->timeout_ms=(frames*1000U+rate-1U)/rate+50U;
 for(unsigned n=0;n<3;n++){
  e=i9100_dma_hold(buffer(s,n));
  if(e){s->fault=1;s->last_error=e;s->cleanup_error=unpin(s);e=I9100_PCM_IO;goto done;}
  s->held_mask|=1U<<n;
 }
 s->prepared=1;
 e=s->io.ensure_muted(s->io.cookie);if(e){e=mute_fault(s,e);goto done;}
 e=i9100_i2s_configure(s->i2s,rate,bclk,verified);s->i2s_owned=s->i2s->prepared;
 if(e){e=fail(s,e,1);goto done;}
 memset((void *)s->period[0]->cpu,0,s->period_bytes);
 memset((void *)s->period[1]->cpu,0,s->period_bytes);
 s->ready_mask=s->next_index=0;s->in_flight=2;
 s->queued_frames=s->completed_frames=s->completed_periods=s->zero_periods=0;
 s->last_error=s->cleanup_error=0;
done:
 s->io.unlock(s->io.cookie);return e;
}
int i9100_pcm_stream_queue(struct i9100_pcm_stream *s,const int16_t *samples,uint32_t frames)
{
 int e=valid(s);if(e)return e;
 if(s->io.lock(s->io.cookie))return I9100_PCM_BUSY;
 if(s->fault){e=I9100_PCM_FAULT;goto done;}
 if(!s->enabled||!s->prepared){e=I9100_PCM_DISABLED;goto done;}
 if(frames>s->period_frames||(frames&&(!samples||((uintptr_t)samples&1U)||
    (uintptr_t)samples>UINTPTR_MAX-frames*4U))){e=I9100_PCM_ARGUMENT;goto done;}
 if(frames){for(unsigned n=0;n<3;n++){
  struct i9100_dma_buffer *b=buffer(s,n);
  if(overlap((uintptr_t)samples,frames*4U,b->cpu,b->bytes)){e=I9100_PCM_OWNER;goto done;}
 }}
 if(s->queued_frames>UINT64_MAX-frames){e=I9100_PCM_OVERFLOW;goto done;}
 unsigned slot=s->next_index;
 if(slot==s->in_flight||(s->ready_mask&(1U<<slot)))slot^=1U;
 if(slot==s->in_flight||(s->ready_mask&(1U<<slot))){e=I9100_PCM_BUSY;goto done;}
 if(!cpu_buffer(s->period[slot],s->period_bytes)){e=fail(s,I9100_PCM_OWNER,1);goto done;}
 memset((void *)s->period[slot]->cpu,0,s->period_bytes);
 if(frames)i9100_pcm_attenuate_30db((int16_t *)s->period[slot]->cpu,samples,frames*2U);
 s->queued_frames+=frames;s->ready_mask|=1U<<slot;e=0;
done:
 s->io.unlock(s->io.cookie);return e;
}
static int submit_next(struct i9100_pcm_stream *s)
{
 unsigned slot=s->next_index;int zero=!(s->ready_mask&(1U<<slot));
 if(s->in_flight<2U||!cpu_buffer(s->period[slot],s->period_bytes)||!cpu_buffer(s->code,64))
  return I9100_PCM_OWNER;
 if(zero){if(s->zero_periods==UINT64_MAX)return I9100_PCM_OVERFLOW;
  memset((void *)s->period[slot]->cpu,0,s->period_bytes);}
 struct i9100_pl330_job job;memset(&job,0,sizeof(job));job.mode=I9100_PL330_I2S_TX;
 job.code=s->code;job.source=s->period[slot];job.bytes=s->period_bytes;job.timeout_ms=s->timeout_ms;
 s->in_flight=slot;s->ready_mask&=~(1U<<slot);
 int e=i9100_pl330_submit(s->dma,&job);if(e)return e;
 if(zero)s->zero_periods++;s->next_index^=1U;return 0;
}
int i9100_pcm_stream_start(struct i9100_pcm_stream *s)
{
 int e=valid(s);if(e)return e;
 if(s->io.lock(s->io.cookie))return I9100_PCM_BUSY;
 if(s->fault){e=I9100_PCM_FAULT;goto done;}
 if(!s->enabled||!s->prepared){e=I9100_PCM_DISABLED;goto done;}
 if(s->started||!s->i2s_owned||s->in_flight<2U||s->i2s->active||
    s->i2s->sample_rate!=s->sample_rate){e=I9100_PCM_BUSY;goto done;}
 e=s->io.ensure_muted(s->io.cookie);if(e){e=mute_fault(s,e);goto done;}
 s->started=1;
 e=submit_next(s);if(e){e=fail(s,e,1);goto done;}
 /* The finite DMA waits for peripheral requests; I2S starts only afterward. */
 e=i9100_i2s_start(s->i2s,0,1);if(e)e=fail(s,e,1);
done:
 s->io.unlock(s->io.cookie);return e;
}
int i9100_pcm_stream_service(struct i9100_pcm_stream *s,uint32_t *completed)
{
 if(completed)*completed=0;int e=valid(s);if(e)return e;if(!completed)return I9100_PCM_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_PCM_BUSY;
 if(s->fault){e=I9100_PCM_FAULT;goto done;}
 if(!s->enabled||!s->started){e=I9100_PCM_DISABLED;goto done;}
 if(s->in_flight>=2U||!our_job(s)){e=fail(s,I9100_PCM_OWNER,1);goto done;}
 if(!s->dma->active&&!s->dma->completed){e=fail(s,I9100_PCM_IO,1);goto done;}
 uint32_t bytes=0;e=i9100_pl330_poll(s->dma,&bytes);
 if(e==I9100_PL330_BUSY){e=I9100_PCM_BUSY;goto done;}
 if(e){e=fail(s,e,1);goto done;}
 if(bytes!=s->period_bytes||s->dma->active||!cpu_buffer(s->period[s->in_flight],s->period_bytes)||!cpu_buffer(s->code,64))
  {e=fail(s,I9100_PCM_OWNER,1);goto done;}
 if(s->completed_periods==UINT64_MAX||s->completed_frames>UINT64_MAX-s->period_frames)
  {e=fail(s,I9100_PCM_OVERFLOW,1);goto done;}
 s->in_flight=2;s->completed_periods++;s->completed_frames+=s->period_frames;*completed=1;
 e=submit_next(s);if(e)e=fail(s,e,1);
done:
 s->io.unlock(s->io.cookie);return e;
}
int i9100_pcm_stream_stop(struct i9100_pcm_stream *s)
{
 int e=valid(s);if(e)return e;if(s->io.lock(s->io.cookie))return I9100_PCM_BUSY;
 e=cleanup(s,0);s->cleanup_error=e;
 if(e){s->fault=1;if(!s->last_error)s->last_error=e;e=I9100_PCM_IO;}
 else s->fault=0;
 s->io.unlock(s->io.cookie);return e;
}
int i9100_pcm_stream_status(struct i9100_pcm_stream *s,struct i9100_pcm_status *out)
{
 int e=valid(s);if(e)return e;if(!out)return I9100_PCM_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_PCM_BUSY;
 struct i9100_pcm_status value;memset(&value,0,sizeof(value));
 value.queued_frames=s->queued_frames;value.completed_frames=s->completed_frames;
 value.completed_periods=s->completed_periods;value.zero_periods=s->zero_periods;
 value.enabled=s->enabled;value.prepared=s->prepared;value.started=s->started;value.fault=s->fault;
 value.held_mask=s->held_mask;value.ready_mask=s->ready_mask;value.in_flight=s->in_flight;
 value.sample_rate=s->sample_rate;value.period_frames=s->period_frames;
 value.last_error=s->last_error;value.cleanup_error=s->cleanup_error;*out=value;
 s->io.unlock(s->io.cookie);return 0;
}
