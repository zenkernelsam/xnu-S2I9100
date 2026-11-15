/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_pl330.h"
#include <string.h>
enum {INTEN=0x20,EVENT_STATUS=0x24,INTCLR=0x2c,MANAGER_FAULT=0x30,
 CHANNEL_FAULT=0x34,DEBUG_STATUS=0xd00,DEBUG_COMMAND=0xd04,
 DEBUG_INS0=0xd08,DEBUG_INS1=0xd0c,CONFIG0=0xe00,PERIPHERAL_ID=0xfe0,CELL_ID=0xff0};
struct encoder {uint8_t *bytes;unsigned used,capacity;int error;};
static void byte(struct encoder *e,unsigned value)
{if(e->used>=e->capacity){e->error=1;return;}e->bytes[e->used++]=(uint8_t)value;}
static void pair(struct encoder *e,unsigned op,unsigned operand){byte(e,op);byte(e,operand);}
static void move(struct encoder *e,unsigned reg,uint32_t value)
{pair(e,0xbc,reg);for(unsigned n=0;n<4;n++)byte(e,value>>(8U*n));}
static void body(struct encoder *e,enum i9100_pl330_mode mode)
{
 if(mode!=I9100_PL330_MEMORY)pair(e,0x32,(mode==I9100_PL330_I2S_TX?12U:11U)<<3);
 if(mode==I9100_PL330_I2S_RX)pair(e,0x27,11U<<3);else byte(e,0x04);
 if(mode==I9100_PL330_I2S_TX)pair(e,0x2b,12U<<3);else byte(e,0x08);
}
static void loop(struct encoder *e,enum i9100_pl330_mode mode,unsigned count)
{
 pair(e,0x20,count-1U);unsigned start=e->used;body(e,mode);
 unsigned distance=e->used-start;pair(e,0x38,distance);
}
static int ram_range(uint32_t address,uint32_t bytes)
{return address>=0x40000000U&&!(address&3U)&&(uint64_t)address+bytes<=0x80000000ULL;}
int i9100_pl330_encode(enum i9100_pl330_mode mode,uint32_t src,uint32_t dst,uint32_t bytes,unsigned event,uint8_t *program,unsigned capacity,unsigned *used)
{
 if(used)*used=0;
 if(!used||!program||capacity<64U||capacity>4096U||event>31U||!bytes||
    bytes>256U*1024U||(bytes&3U)||mode<I9100_PL330_MEMORY||mode>I9100_PL330_I2S_RX)
  return I9100_PL330_ARGUMENT;
 if(mode!=I9100_PL330_I2S_RX&&!ram_range(src,bytes))return I9100_PL330_ARGUMENT;
 if(mode!=I9100_PL330_I2S_TX&&!ram_range(dst,bytes))return I9100_PL330_ARGUMENT;
 if((mode==I9100_PL330_I2S_TX&&dst!=0x03830010U)||
    (mode==I9100_PL330_I2S_RX&&src!=0x03830014U))return I9100_PL330_ARGUMENT;
 uint8_t scratch[64];struct encoder e={scratch,0,sizeof(scratch),0};
 uint32_t control=(2U<<1)|(2U<<15)|(1U<<9)|(1U<<23);
 if(mode!=I9100_PL330_I2S_RX)control|=1U;
 if(mode!=I9100_PL330_I2S_TX)control|=1U<<14;
 move(&e,1,control);move(&e,0,src);move(&e,2,dst);
 if(mode!=I9100_PL330_MEMORY)pair(&e,0x35,(mode==I9100_PL330_I2S_TX?12U:11U)<<3);
 unsigned words=bytes/4U,blocks=words/256U,remainder=words%256U;
 if(blocks){pair(&e,0x22,blocks-1U);unsigned outer=e.used;loop(&e,mode,256U);pair(&e,0x3c,e.used-outer);}
 if(remainder)loop(&e,mode,remainder);
 byte(&e,0x13);pair(&e,0x34,event<<3);byte(&e,0);
 if(e.error||e.used>capacity)return I9100_PL330_ARGUMENT;
 memcpy(program,scratch,e.used);*used=e.used;return 0;
}
static uint32_t rd(struct i9100_pl330 *p,unsigned off){return p->io.read32(p->io.cookie,off);}
static void wr(struct i9100_pl330 *p,unsigned off,uint32_t value){p->io.write32(p->io.cookie,off,value);}
static unsigned state(struct i9100_pl330 *p){return rd(p,0x100U+p->channel*8U)&15U;}
static int wait_debug(struct i9100_pl330 *p)
{
 uint64_t started=p->io.now_ns(p->io.cookie);
 for(unsigned n=0;n<2500U;n++){
  if(!(rd(p,DEBUG_STATUS)&1U))return 0;
  uint64_t now=p->io.now_ns(p->io.cookie);
  if(now<started||now-started>5000000ULL)break;
  if(p->io.delay_us(p->io.cookie,2))return I9100_PL330_IO;
 }
 return I9100_PL330_TIMEOUT;
}
static int debug(struct i9100_pl330 *p,int kill,uint32_t address)
{
 int e=wait_debug(p);if(e)return e;
 uint32_t instruction=kill?(1U<<16)|1U|(p->channel<<8):(0xa2U<<16)|(p->channel<<24);
 wr(p,DEBUG_INS0,instruction);wr(p,DEBUG_INS1,kill?0:address);wr(p,DEBUG_COMMAND,0);
 return wait_debug(p);
}
int i9100_pl330_init(struct i9100_pl330 *p,const struct i9100_pl330_io *io,unsigned channel,unsigned event)
{
 if(!p||p->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||!io->now_ns||!io->delay_us||channel>7U||event>31U)
  return I9100_PL330_ARGUMENT;
 memset(p,0,sizeof(*p));p->io=*io;p->channel=channel;p->event=event;p->initialized=1;return 0;
}
static uint32_t id(struct i9100_pl330 *p,unsigned off)
{uint32_t value=0;for(unsigned n=0;n<4;n++)value|=(rd(p,off+4U*n)&255U)<<(8U*n);return value;}
int i9100_pl330_enable(struct i9100_pl330 *p,int enabled)
{
 if(!p||!p->initialized||(enabled!=0&&enabled!=1))return I9100_PL330_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PL330_BUSY;
 int e=0;
 if(enabled){
  if(p->enabled||p->active||p->fault){e=I9100_PL330_BUSY;goto done;}
  uint32_t pid=id(p,PERIPHERAL_ID),cid=id(p,CELL_ID),config=rd(p,CONFIG0);
  if((pid&0xfffffU)!=0x41330U||cid!=0xb105f00dU){e=I9100_PL330_ID;goto done;}
  p->channels=((config>>4)&7U)+1U;p->events=((config>>17)&31U)+1U;
  p->peripherals=config&1U?((config>>12)&31U)+1U:0;
  if(p->channel>=p->channels||p->event>=p->events){e=I9100_PL330_ARGUMENT;goto done;}
  if(state(p)||(rd(p,DEBUG_STATUS)&1U)||rd(p,MANAGER_FAULT)||
    (rd(p,CHANNEL_FAULT)&(1U<<p->channel))||(rd(p,EVENT_STATUS)&(1U<<p->event))||
    (rd(p,INTEN)&(1U<<p->event))){e=I9100_PL330_BUSY;goto done;}
  p->saved_inten=rd(p,INTEN)&(1U<<p->event);
  wr(p,INTEN,rd(p,INTEN)|(1U<<p->event));
  if(!(rd(p,INTEN)&(1U<<p->event))){e=I9100_PL330_IO;p->fault=1;goto done;}
  p->enabled=1;
 }else if(!p->enabled)e=I9100_PL330_DISABLED;
 else if(p->active||state(p)||(rd(p,DEBUG_STATUS)&1U))e=I9100_PL330_BUSY;
 else{wr(p,INTEN,(rd(p,INTEN)&~(1U<<p->event))|p->saved_inten);
  if((rd(p,INTEN)&(1U<<p->event))!=p->saved_inten)e=I9100_PL330_IO;else p->enabled=0;}
done:
 p->io.unlock(p->io.cookie);return e;
}
int i9100_pl330_idle(void *cookie)
{
 struct i9100_pl330 *p=cookie;
 if(!p||!p->initialized)return -1;
 return state(p)||(rd(p,DEBUG_STATUS)&1U)?-1:0;
}
static int overlap(struct i9100_dma_buffer *a,struct i9100_dma_buffer *b)
{return a&&b&&a->physical<(uint64_t)b->physical+b->bytes&&b->physical<(uint64_t)a->physical+a->bytes;}
static int valid_buffer(struct i9100_dma_buffer *b,uint32_t offset,uint32_t bytes)
{return b&&b->self==(uintptr_t)b&&b->state==I9100_DMA_CPU&&offset<=b->bytes&&bytes<=b->bytes-offset&&!(offset&3U);}
static int take_buffers(struct i9100_pl330 *p,int aborted)
{
 int e=0;struct i9100_dma_buffer *buffers[3]={p->job.source,p->job.destination,p->job.code};
 for(unsigned n=0;n<3;n++)if(buffers[n]){
  struct i9100_dma_buffer *b=buffers[n];
  if(b->state==I9100_DMA_DEVICE||b->state==I9100_DMA_QUARANTINED){
   int error=aborted?i9100_dma_abort(b):i9100_dma_complete(b);if(error)e=I9100_PL330_IO;
  }else if(b->state!=I9100_DMA_CPU)e=I9100_PL330_IO;
 }
 if(!e){p->active=0;memset(&p->job,0,sizeof(p->job));}
 return e;
}
static int clear_event(struct i9100_pl330 *p)
{
 wr(p,INTCLR,1U<<p->event);
 return rd(p,EVENT_STATUS)&(1U<<p->event)?I9100_PL330_IO:0;
}
static int stop_locked(struct i9100_pl330 *p)
{
 if(!p->active){
  if(i9100_pl330_idle(p))return I9100_PL330_BUSY;
  if(rd(p,MANAGER_FAULT)||(rd(p,CHANNEL_FAULT)&(1U<<p->channel)))return I9100_PL330_FAULT;
  int error=clear_event(p);if(error){p->fault=1;return error;}
  p->fault=p->completed=0;return 0;
 }
 int e=debug(p,1,0);
 if(!e){uint64_t start=p->io.now_ns(p->io.cookie);
  for(unsigned n=0;n<25000U;n++){
   if(!state(p))break;
   uint64_t now=p->io.now_ns(p->io.cookie);
   if(now<start||now-start>50000000ULL){e=I9100_PL330_TIMEOUT;break;}
   if(p->io.delay_us(p->io.cookie,2)){e=I9100_PL330_IO;break;}
  }
  if(state(p))e=I9100_PL330_TIMEOUT;
 }
 if(!e&&i9100_pl330_idle(p))e=I9100_PL330_BUSY;
 if(e){
  struct i9100_dma_buffer *buffers[3]={p->job.source,p->job.destination,p->job.code};
  for(unsigned n=0;n<3;n++)if(buffers[n]&&(buffers[n]->state==I9100_DMA_DEVICE||buffers[n]->state==I9100_DMA_QUARANTINED))
   (void)i9100_dma_abort(buffers[n]);
  p->fault=1;return e;
 }
 e=clear_event(p);if(!e)e=take_buffers(p,1);
 if(rd(p,MANAGER_FAULT)||(rd(p,CHANNEL_FAULT)&(1U<<p->channel)))e=I9100_PL330_FAULT;
 p->fault=e?1U:0U;return e;
}
int i9100_pl330_submit(struct i9100_pl330 *p,const struct i9100_pl330_job *job)
{
 if(!p||!p->initialized||!job||!job->bytes||job->bytes>256U*1024U||(job->bytes&3U)||
    !job->timeout_ms||job->timeout_ms>2000U||job->mode<I9100_PL330_MEMORY||job->mode>I9100_PL330_I2S_RX)
  return I9100_PL330_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PL330_BUSY;
 int e=0;
 if(!p->enabled){e=I9100_PL330_DISABLED;goto done;}
 if(p->active||p->fault||i9100_pl330_idle(p)||rd(p,MANAGER_FAULT)||
    (rd(p,CHANNEL_FAULT)&(1U<<p->channel))){e=I9100_PL330_BUSY;goto done;}
 if(!valid_buffer(job->code,0,64)||
    (job->mode!=I9100_PL330_I2S_RX&&!valid_buffer(job->source,job->source_offset,job->bytes))||
    (job->mode!=I9100_PL330_I2S_TX&&!valid_buffer(job->destination,job->destination_offset,job->bytes))||
    (job->mode==I9100_PL330_I2S_RX&&job->source)||(job->mode==I9100_PL330_I2S_TX&&job->destination)||
    overlap(job->code,job->source)||overlap(job->code,job->destination)||overlap(job->source,job->destination)||
    (job->mode!=I9100_PL330_MEMORY&&p->peripherals<=12U)){e=I9100_PL330_ARGUMENT;goto done;}
 uint32_t src=job->mode==I9100_PL330_I2S_RX?0x03830014U:job->source->physical+job->source_offset;
 uint32_t dst=job->mode==I9100_PL330_I2S_TX?0x03830010U:job->destination->physical+job->destination_offset;
 unsigned used=0;e=i9100_pl330_encode(job->mode,src,dst,job->bytes,p->event,(uint8_t *)job->code->cpu,job->code->bytes<4096U?job->code->bytes:4096U,&used);
 if(e)goto done;
 p->job=*job;p->program_bytes=used;p->active=1;p->completed=p->error_status=0;
 e=i9100_dma_prepare(job->code,I9100_DMA_TO_DEVICE);
 if(!e&&job->source)e=i9100_dma_prepare(job->source,I9100_DMA_TO_DEVICE);
 if(!e&&job->destination)e=i9100_dma_prepare(job->destination,I9100_DMA_FROM_DEVICE);
 if(e){e=I9100_PL330_IO;p->fault=1;
  if(job->mode==I9100_PL330_MEMORY)(void)stop_locked(p);goto done;}
 e=clear_event(p);
 if(e){p->fault=1;if(job->mode==I9100_PL330_MEMORY)(void)stop_locked(p);goto done;}
 p->started=p->io.now_ns(p->io.cookie);
 e=debug(p,0,job->code->physical);
 if(e){p->fault=1;if(job->mode==I9100_PL330_MEMORY)(void)stop_locked(p);}
done:
 p->io.unlock(p->io.cookie);return e;
}
int i9100_pl330_poll(struct i9100_pl330 *p,uint32_t *completed)
{
 if(completed)*completed=0;
 if(!p||!p->initialized||!completed)return I9100_PL330_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PL330_BUSY;
 int e=0;
 if(!p->enabled)e=I9100_PL330_DISABLED;
 else if(!p->active){e=p->completed?0:I9100_PL330_BUSY;*completed=p->completed;}
 else if(p->fault)e=I9100_PL330_FAULT;
 else if(rd(p,MANAGER_FAULT)||(rd(p,CHANNEL_FAULT)&(1U<<p->channel))){
  p->error_status=rd(p,0x40U+p->channel*4U);e=I9100_PL330_FAULT;p->fault=1;
  if(p->job.mode==I9100_PL330_MEMORY)(void)stop_locked(p);
 }else if((rd(p,EVENT_STATUS)&(1U<<p->event))&&!state(p)){
  uint32_t bytes=p->job.bytes;e=clear_event(p);if(!e)e=take_buffers(p,0);
  if(!e){p->completed=bytes;*completed=bytes;}else p->fault=1;
 }else {
  uint64_t now=p->io.now_ns(p->io.cookie);
  if(now<p->started||now-p->started>(uint64_t)p->job.timeout_ms*1000000ULL){e=I9100_PL330_TIMEOUT;p->fault=1;
   if(p->job.mode==I9100_PL330_MEMORY)(void)stop_locked(p);}
  else e=I9100_PL330_BUSY;
 }
 p->io.unlock(p->io.cookie);return e;
}
int i9100_pl330_stop(struct i9100_pl330 *p)
{
 if(!p||!p->initialized)return I9100_PL330_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PL330_BUSY;
 int e=stop_locked(p);p->io.unlock(p->io.cookie);return e;
}
