/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_fimc.h"
#include <string.h>
enum {SRC,WINDOW,GCTRL,WINDOW2,YADDR,CBADDR,CRADDR,TARGET,OUTPUT,PRE_RATIO,PRE_SIZE,SCALER,
 AREA,CAPTURE,EFFECT,INPUT,YOFF,CBOFF,CROFF,ORIG_SIZE,EXTENSION,DMA_STYLE,CSI_FORMAT,SEQUENCE};
#define CAPTURE_ENABLE ((1U<<31)|(1U<<30))
#define FIMC_BUSY ((1U<<26)|(1U<<22)|(1U<<21))
#define FRAME_END (1U<<17)
#define LAST_END (1U<<16)
#define FIMC_ERRORS ((7U<<29)|(1U<<18))
#define GCTRL_MASK ((1U<<29)|(3U<<27)|(7U<<24)|(1U<<22)|(1U<<21)|(1U<<20)|(1U<<18)|(1U<<17)|(1U<<16)|(1U<<12)|(1U<<10)|(1U<<8)|(1U<<7)|(1U<<6)|(1U<<5)|(1U<<4)|(1U<<3)|1U)
/* Slot0's three addresses are at 18/28/38. Both the v5.1 32-slot
 * enable bitmap and the MIPI format have owned restoration records. */
static uint32_t rd(struct i9100_fimc *c,unsigned off){return c->io.read32(c->io.cookie,off);}
static void wr(struct i9100_fimc *c,unsigned off,uint32_t v)
{c->io.write32(c->io.cookie,off,v);c->io.barrier(c->io.cookie);}
static int valid(struct i9100_fimc *c)
{return !c||!c->initialized?I9100_CE_ARGUMENT:c->self!=(uintptr_t)c?I9100_CE_OWNER:0;}
static int note(struct i9100_fimc *c,int e){if(e&&!c->first_error)c->first_error=e;return e;}
static int irq_clear(struct i9100_fimc *c)
{
 uint32_t v=rd(c,8);if(v==UINT32_MAX)return I9100_CE_IO;
 if(v&(1U<<31))return I9100_CE_BUSY;
 if((v&c->regs[GCTRL].mask)!=c->regs[GCTRL].last)return I9100_CE_OWNER;
 wr(c,8,v|(1U<<19));v=rd(c,8);
 return v==UINT32_MAX?I9100_CE_IO:(v&c->regs[GCTRL].mask)!=c->regs[GCTRL].last?I9100_CE_OWNER:0;
}
int i9100_fimc_idle(void *cookie)
{
 struct i9100_fimc *c=cookie;if(valid(c))return -1;
 uint32_t cap=rd(c,0xc0),sc=rd(c,0x58),in=rd(c,0xfc),status=rd(c,0x64),global=rd(c,8);
 if(cap==UINT32_MAX||sc==UINT32_MAX||in==UINT32_MAX||status==UINT32_MAX||global==UINT32_MAX)return -1;
 return (cap&CAPTURE_ENABLE)||(sc&(1U<<15))||(in&1U)||(status&FIMC_BUSY)||(global&(1U<<31))?-1:0;
}
static int stop_engine(struct i9100_fimc *c)
{
 int e=i9100_camera_reg_set(&c->io,&c->regs[CAPTURE],c->regs[CAPTURE].last&~CAPTURE_ENABLE);
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[SCALER],c->regs[SCALER].last&~(1U<<15));
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[INPUT],c->regs[INPUT].last&~1U);
 if(e)return e;
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(unsigned n=0;n<100U;n++){
  if(!i9100_fimc_idle(c)){
   e=i9100_camera_reg_set(&c->io,&c->regs[SEQUENCE],0);
   if(!e)c->running=0;return e;
  }
  uint64_t t=c->io.now_ns(c->io.cookie);if(t<start||t-start>=100000000ULL)break;
  if(c->io.delay_us(c->io.cookie,1000))return I9100_CE_IO;
 }return I9100_CE_TIMEOUT;
}
static int release_buffer(struct i9100_fimc *c,int complete)
{
 if(!c->buffer)return 0;
 int e=0;
 if(c->buffer->state==I9100_DMA_DEVICE||c->buffer->state==I9100_DMA_QUARANTINED)
  e=complete?i9100_dma_complete(c->buffer):i9100_dma_abort(c->buffer);
 if(e)return I9100_CE_IO;
 if(i9100_fimc_idle(c))return I9100_CE_BUSY;
 c->prepared=0;
 if(c->held){if(i9100_dma_unhold(c->buffer))return I9100_CE_IO;c->held=0;}
 c->buffer=0;c->ready=c->frame_observed=0;return 0;
}
static int stop_locked(struct i9100_fimc *c)
{
 if(c->consumer)return I9100_CE_BUSY;
 int e=c->owned?stop_engine(c):0;
 /* Even a failed stop runs the guard's idle check: active memory becomes
  * quarantined, never CPU-owned simply because our cached running is zero. */
 if(c->buffer&&!c->ready){
  int b=release_buffer(c,0);if(!e)e=b;
 }else if(!e&&c->buffer)e=release_buffer(c,0);
 c->cleanup_error=e;return e;
}
int i9100_fimc_init(struct i9100_fimc *c,const struct i9100_camera_engine_io *io)
{
 if(!c||c->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||!io->barrier||!io->now_ns||!io->delay_us)
  return I9100_CE_ARGUMENT;
 memset(c,0,sizeof(*c));c->io=*io;c->self=(uintptr_t)c;c->initialized=1;
 const unsigned offsets[24]={0,4,8,0x14,0x18,0x28,0x38,0x48,0x4c,0x50,0x54,0x58,
  0x5c,0xc0,0xd0,0xfc,0x168,0x16c,0x170,0x184,0x188,0x18c,0x194,0x1fc};
 const uint32_t masks[24]={0xafffcfffU,0x87ff0fffU,GCTRL_MASK,0x0fff0fffU,
  UINT32_MAX,UINT32_MAX,UINT32_MAX,0xefffefffU,0x4303000fU,UINT32_MAX,UINT32_MAX,
  UINT32_MAX,0x0fffffffU,CAPTURE_ENABLE|(1U<<25)|(1U<<18),(1U<<30),9U,
  UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,(1U<<26)|(1U<<24)|(1U<<22)|(0x3fU<<10)|0x3fU,3U<<13,0x13fU,UINT32_MAX};
 for(unsigned n=0;n<24;n++){c->regs[n].offset=offsets[n];c->regs[n].mask=masks[n];}return 0;
}
int i9100_fimc_enable(struct i9100_fimc *c,int enabled,int verified)
{
 int e=valid(c);if(e)return e;if(enabled!=0&&enabled!=1)return I9100_CE_ARGUMENT;
 if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(enabled){
  if(verified!=1){e=I9100_CE_ARGUMENT;goto out;}
  if(c->owned||c->enabled||c->generation==UINT32_MAX||c->buffer){e=I9100_CE_BUSY;goto out;}
  if(i9100_fimc_idle(c)||rd(c,8)&((1U<<16)|(1U<<17))||rd(c,0x1fc)){e=I9100_CE_BUSY;goto out;}
  for(unsigned n=0;n<24&&!e;n++)e=i9100_camera_reg_save(&c->io,&c->regs[n]);
  if(!e){c->generation++;c->owned=c->enabled=1;c->first_error=c->cleanup_error=0;}
 }else{
  e=stop_locked(c);
  for(unsigned n=0;n<24&&!e;n++)e=i9100_camera_reg_restore(&c->io,&c->regs[n]);
  if(!e)c->owned=c->enabled=0;else c->cleanup_error=e;
 }
out:c->io.unlock(c->io.cookie);return e;
}
static int program(struct i9100_fimc *c,const struct i9100_fimc_config *f)
{
 const uint32_t values[24]={
  (1U<<31)|(f->width<<16)|f->height|(f->input_order422<<14),0,
  (1U<<20)|(1U<<18)|(1U<<3)|(f->csis_index==0U?(1U<<7):0U)|(f->invert_vsync<<25),0,
  c->buffer->physical,0,0,(2U<<29)|(f->width<<16)|f->height,3U|(1U<<30),
  (10U<<28)|(1U<<16)|1U,(f->width<<16)|f->height,
  (1U<<31)|(1U<<9)|(0x100U<<16)|0x100U,f->width*f->height,0,0,0,0,0,0,
  (f->height<<16)|f->width,0,0,0x11eU,0};
 int e=0;for(unsigned n=0;n<24&&!e;n++)e=i9100_camera_reg_set(&c->io,&c->regs[n],values[n]);
 return e;
}
int i9100_fimc_capture(struct i9100_fimc *c,const struct i9100_fimc_config *f,struct i9100_dma_buffer *b)
{
 int e=valid(c);if(e)return e;
 if(!f||f->width<16U||(f->width&7U)||f->height<16U||f->csis_index>1U||f->input_order422>3U||
    f->alignment!=32U||f->invert_vsync>1U||f->timeout_ms<10U||f->timeout_ms>1000U||!b||
    b->self!=(uintptr_t)b||b->state!=I9100_DMA_CPU)return I9100_CE_ARGUMENT;
 struct i9100_video_layout layout;
 if(i9100_video_layout(I9100_VIDEO_YUYV,f->width,f->height,&layout)||layout.total_bytes>b->bytes)return I9100_CE_ARGUMENT;
 if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(!c->enabled){e=I9100_CE_DISABLED;goto out;}
 if(c->first_error||c->buffer||c->running||c->ready||c->ticket==UINT32_MAX){e=I9100_CE_BUSY;goto out;}
 if(i9100_fimc_idle(c)||rd(c,0x1fc)){e=I9100_CE_BUSY;goto out;}
 if(i9100_dma_hold(b)){e=I9100_CE_OWNER;goto out;}c->buffer=b;c->held=1;
 c->config=*f;c->layout=layout;c->ticket++;c->polls=c->frame_observed=c->status=c->status2=0;
 e=program(c,f);if(e)goto fail;
 /* FIMC status reset is a write-zero clear, not CSIS's W1C. Only after
  * exclusive idle, clear old FRAMEEND/LASTCAPT_END; never count a stale one. */
 uint32_t v=rd(c,0x64);if(v==UINT32_MAX||(v&FIMC_ERRORS)){e=I9100_CE_FRAME;goto fail;}
 wr(c,0x64,v&~(FRAME_END|LAST_END));
 if(rd(c,0x64)&(FRAME_END|LAST_END)){e=I9100_CE_IO;goto fail;}
 if(i9100_dma_prepare(b,I9100_DMA_FROM_DEVICE)){e=I9100_CE_IO;goto fail;}c->prepared=1;
 e=i9100_camera_reg_set(&c->io,&c->regs[SEQUENCE],1U);if(e)goto fail;
 c->started=c->io.now_ns(c->io.cookie);c->running=1;
 e=i9100_camera_reg_set(&c->io,&c->regs[CAPTURE],(1U<<31));
 if(e)goto fail;goto out;
fail:note(c,e);c->cleanup_error=stop_locked(c);
out:c->io.unlock(c->io.cookie);return e;
}
int i9100_fimc_poll(struct i9100_fimc *c)
{
 int e=valid(c);if(e)return e;if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(!c->enabled){e=I9100_CE_DISABLED;goto out;}if(c->first_error){e=c->first_error;goto out;}
 if(c->ready)goto out;if(!c->running){e=I9100_CE_BUSY;goto out;}
 uint32_t status=rd(c,0x64),slot=rd(c,0x68);c->status=status;c->status2=slot;
 if(status==UINT32_MAX||slot==UINT32_MAX){e=I9100_CE_IO;goto fail;}
 if(status&FIMC_ERRORS){e=I9100_CE_FRAME;goto fail;}
 if(c->polls!=UINT32_MAX)c->polls++;
 uint64_t t=c->io.now_ns(c->io.cookie);
 if(t<c->started||t-c->started>c->config.timeout_ms*1000000ULL||c->polls>10000U){e=I9100_CE_TIMEOUT;goto fail;}
 if(!(status&FRAME_END)){e=I9100_CE_BUSY;goto out;}
 c->frame_observed=1;e=stop_engine(c);if(e)goto fail;
 /* v5.1 before/present slots are one-based. Slot0 is the sole enabled
  * allocation, and a final stable before=1 must describe its completed DMA. */
 status=rd(c,0x64);slot=rd(c,0x68);c->status=status;c->status2=slot;
 if(status==UINT32_MAX||slot==UINT32_MAX||(status&FIMC_ERRORS)||!(status&FRAME_END)||!(status&LAST_END)||
    ((slot>>7)&63U)!=1U||(slot&63U)>1U||rd(c,0x68)!=slot||
    rd(c,0x18)!=c->buffer->physical||rd(c,0x1fc)){e=I9100_CE_FRAME;goto fail;}
 e=irq_clear(c);if(e)goto fail;
 if(i9100_dma_complete(c->buffer)){e=I9100_CE_IO;goto fail;}
 c->prepared=0;c->ready=1;c->completed_ns=c->io.now_ns(c->io.cookie);
 goto out;
fail:note(c,e);c->cleanup_error=stop_locked(c);
out:c->io.unlock(c->io.cookie);return e;
}
int i9100_fimc_take_frame(struct i9100_fimc *c,struct i9100_camera_frame *frame)
{
 int e=valid(c);if(e)return e;if(!frame||frame->self)return I9100_CE_ARGUMENT;
 if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(!c->ready||!c->held||c->consumer||c->first_error||!c->buffer||c->buffer->state!=I9100_DMA_CPU)e=I9100_CE_BUSY;
 else{
  memset(frame,0,sizeof(*frame));frame->self=(uintptr_t)frame;frame->engine=(uintptr_t)c;frame->buffer=c->buffer;
  frame->layout=c->layout;frame->completed_ns=c->completed_ns;frame->generation=c->generation;frame->ticket=c->ticket;
  frame->bytes=c->layout.pitch*c->layout.height;frame->status=c->status;frame->status2=c->status2;c->consumer=frame;
 }c->io.unlock(c->io.cookie);return e;
}
int i9100_fimc_release_frame(struct i9100_fimc *c,struct i9100_camera_frame *frame)
{
 int e=valid(c);if(e)return e;if(!frame)return I9100_CE_ARGUMENT;
 if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(frame->self!=(uintptr_t)frame||frame->engine!=(uintptr_t)c||c->consumer!=frame||frame->buffer!=c->buffer||
    frame->generation!=c->generation||frame->ticket!=c->ticket)e=I9100_CE_OWNER;
 else if(!c->buffer||c->buffer->state!=I9100_DMA_CPU||i9100_fimc_idle(c))e=I9100_CE_BUSY;
 else if(i9100_dma_unhold(c->buffer))e=I9100_CE_IO;
 else{c->held=c->ready=c->frame_observed=0;c->buffer=0;c->consumer=0;memset(frame,0,sizeof(*frame));}
 c->io.unlock(c->io.cookie);return e;
}
int i9100_fimc_stop(struct i9100_fimc *c)
{
 int e=valid(c);if(e)return e;if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 e=stop_locked(c);if(e&&e!=I9100_CE_BUSY)note(c,e);c->io.unlock(c->io.cookie);return e;
}
