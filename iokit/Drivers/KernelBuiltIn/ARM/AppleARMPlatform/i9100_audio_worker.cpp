/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_worker.h"
#include "i9100_irq_router.h"
#include "i9100_pcm_ring.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <kern/assert.h>
#include <string.h>
struct i9100_audio_worker;
static void process(i9100_audio_worker *);
static int capture_pcm(i9100_audio_worker *);
static int halt(i9100_audio_worker *);
class I9100AudioEvents : public IOEventSource {
 OSDeclareDefaultStructors(I9100AudioEvents)
 i9100_audio_worker *client;
public:
 bool configure(i9100_audio_worker *w){client=w;return IOEventSource::init((OSObject *)w,0);}
 void wake(){signalWorkAvailable();}
 virtual bool checkForWork();
};
OSDefineMetaClassAndStructors(I9100AudioEvents,IOEventSource)
struct audio_worker_state {
 i9100_audio_board_native *board;IOWorkLoop *loop;I9100AudioEvents *event;IOTimerEventSource *timer;
 i9100_audio_board_worker_lease board_lease;uintptr_t self;
 IOSimpleLock *guard;i9100_irq_lease irq;
 uint64_t completions;
 uint32_t running,fault,stopping,pending,irq_wakes,timer_wakes,poll_us,added,board_held,session_owned;
 i9100_irq_registry *registry;uint32_t router_held;
 int32_t error;
 i9100_pcm_status last_pcm;uint32_t last_pcm_valid;
 i9100_pcm_ring ring;int16_t *storage,*scratch;uint32_t storage_bytes,period_frames;
 struct i9100_audio_board_status last_board;uint32_t last_board_valid;
 uint32_t output_requested,output_prepared,output_active,closed,detaching;int32_t cleanup_error,irq_error;
 uint32_t gated,flush_pending;
};
struct i9100_audio_worker : public OSObject {
 OSDeclareDefaultStructors(i9100_audio_worker)
public:
 audio_worker_state s;
 bool initialize(){memset(&s,0,sizeof(s));s.self=(uintptr_t)this;return OSObject::init();}
 virtual void free();
};
OSDefineMetaClassAndStructors(i9100_audio_worker,OSObject)
static IOInterruptState enter(i9100_audio_worker *w){return IOSimpleLockLockDisableInterrupt(w->s.guard);}
static void leave(i9100_audio_worker *w,IOInterruptState s){IOSimpleLockUnlockEnableInterrupt(w->s.guard,s);}
bool I9100AudioEvents::checkForWork()
{
 IOInterruptState s=enter(client);bool ready=enabled&&client->s.pending&&client->s.running&&!client->s.stopping;
 client->s.pending=0;leave(client,s);if(ready)process(client);return false;
}
static void fault(i9100_audio_worker *w,int e)
{
 IOInterruptState s=enter(w);if(!w->s.fault)w->s.error=e;w->s.fault=1;w->s.running=0;leave(w,s);
 /* All callers are on the workloop, including deferred IRQ errors. Timer
  * scheduling alone cannot stop a powered DMA/codec output. */
 w->s.cleanup_error=halt(w);
}
static int feed(i9100_audio_worker *w,int flush)
{
 if(!w->s.storage)return 0;
 flush=flush||w->s.flush_pending;
 if(w->s.output_prepared&&!w->s.output_active)return 0; /* hold producer data during real zero pre-roll/mute */
 for(unsigned attempt=0;attempt<2U;attempt++){
  uint32_t frames=w->s.ring.frames<w->s.period_frames?w->s.ring.frames:w->s.period_frames;
  if(!frames){w->s.flush_pending=0;return 0;}if(!flush&&frames<w->s.period_frames)return 0;
  int e=i9100_pcm_ring_peek(&w->s.ring,w->s.scratch,frames);if(e)return e;
  e=i9100_audio_board_worker_queue(&w->s.board_lease,w->s.scratch,frames);if(e==I9100_PCM_BUSY)return 0;if(e)return e;
  e=i9100_pcm_ring_consume(&w->s.ring,frames);if(e)return e;
 }
 if(!w->s.ring.frames)w->s.flush_pending=0;
 return 0;
}
static int capture_pcm(i9100_audio_worker *w)
{
 if(!w->s.board||w->s.closed)return I9100_AP_DISABLED;
 int fresh=i9100_audio_board_status(w->s.board,&w->s.last_board,0);if(!fresh)w->s.last_board_valid=1;
 struct i9100_audio_board_status bs;
 if(!i9100_audio_board_status(w->s.board,&bs,&w->s.last_pcm))w->s.last_pcm_valid=1;return fresh;
}
static void process(i9100_audio_worker *w)
{
 IOInterruptState s=enter(w);bool run=w->s.running&&!w->s.stopping&&!w->s.fault&&!w->s.closed;
 int irq_error=w->s.irq_error;leave(w,s);if(!run)return;if(irq_error){fault(w,irq_error);return;}
 int e=feed(w,0);if(e){fault(w,e);return;}
 uint32_t completed=0;e=i9100_audio_board_worker_service(&w->s.board_lease,&completed);
 capture_pcm(w);
 if(completed){if(w->s.completions>~(uint64_t)0-completed){fault(w,I9100_PCM_OVERFLOW);return;}w->s.completions+=completed;}
 if(e&&e!=I9100_PCM_BUSY){fault(w,e);return;}
 e=feed(w,0);if(e){fault(w,e);return;}
 if(w->s.gated){e=i9100_audio_board_worker_release_period(&w->s.board_lease);
  if(e&&e!=I9100_PCM_BUSY){fault(w,e);return;}capture_pcm(w);}
 if(completed&&w->s.irq.active){e=i9100_irq_unmask(&w->s.irq);if(e){fault(w,e);return;}}
 if(w->s.timer->setTimeoutUS(w->s.poll_us)!=kIOReturnSuccess)fault(w,I9100_AP_IO);
}
static void timer_action(OSObject *owner,IOTimerEventSource *)
{i9100_audio_worker *w=(i9100_audio_worker *)owner;if(w->s.closed||w->s.stopping)return;w->s.timer_wakes++;process(w);}
static int interrupt(void *cookie,uint32_t ack)
{
 i9100_audio_worker *w=(i9100_audio_worker *)cookie;if((ack&1023U)!=67U)return I9100_IRQ_ARGUMENT;
 int e=i9100_irq_mask(&w->s.irq);
 IOInterruptState s=enter(w);bool wake=w->s.running&&!w->s.stopping&&!w->s.fault;
 if(w->s.irq_wakes!=0xffffffffU)w->s.irq_wakes++;if(wake)w->s.pending=1;leave(w,s);
 if(e){s=enter(w);if(!w->s.irq_error)w->s.irq_error=e;leave(w,s);}
 if(wake)w->s.event->wake();return e;
}
enum {PREPARE=1,QUEUE,START,STOP,STATUS,DETACH,ENQUEUE,FLUSH,OUTPUT_PREPARE,OUTPUT_ACTIVATE,OUTPUT_MUTE};
static int halt(i9100_audio_worker *w)
{
 if(w->s.closed)return 0;
 IOInterruptState s=enter(w);w->s.stopping=1;w->s.running=0;w->s.pending=0;leave(w,s);
 w->s.event->disable();w->s.timer->disable();w->s.timer->cancelTimeout();
 int e=w->s.irq.active?i9100_irq_mask(&w->s.irq):0;
 /* An IRQ mask failure must not prevent independent silence/DMA shutdown.
  * The worker/lease remain retained until mask/remove later succeeds. */
 if(w->s.session_owned||w->s.output_requested){
  capture_pcm(w);int closed=i9100_audio_board_worker_pcm_close(&w->s.board_lease);
  capture_pcm(w);if(closed){if(!e)e=closed;}
  else{w->s.session_owned=0;w->s.output_requested=w->s.output_prepared=w->s.output_active=0;}
 }
 return e;
}
static int stop(i9100_audio_worker *w)
{
 int e=halt(w);w->s.cleanup_error=e;if(e)return e;
 if(w->s.storage){memset(&w->s.ring,0,sizeof(w->s.ring));
  e=i9100_pcm_ring_init(&w->s.ring,w->s.storage,w->s.period_frames*8U);if(e)return e;}
 w->s.flush_pending=0;
 IOInterruptState s=enter(w);w->s.fault=w->s.stopping=0;w->s.irq_error=0;leave(w,s);return 0;
}
static IOReturn control(OSObject *target,void *operation,void *arg0,void *arg1,void *)
{
 i9100_audio_worker *w=(i9100_audio_worker *)target;int e=0;IOInterruptState saved;
 if((w->s.closed||w->s.detaching)&&(uintptr_t)operation!=STATUS&&(uintptr_t)operation!=DETACH)return I9100_AP_DISABLED;
 switch((uintptr_t)operation){
 case PREPARE:
  if(w->s.running||w->s.stopping||w->s.fault||w->s.session_owned)return I9100_AP_BUSY;
  /* The exact board lease excludes a foreign PCM opener, so this attempted
   * open owns any partial result and is always covered by explicit cleanup. */
  w->s.session_owned=1;e=i9100_audio_board_worker_pcm_open(&w->s.board_lease,(uint32_t)(uintptr_t)arg0);
  if(e){fault(w,e);return e;}w->s.error=w->s.cleanup_error=0;w->s.last_pcm_valid=w->s.last_board_valid=0;
  {struct i9100_audio_board_status bs;i9100_pcm_status pcm;
   e=i9100_audio_board_status(w->s.board,&bs,&pcm);if(e){fault(w,e);return e;}
   if(pcm.period_frames<16U||pcm.period_frames>4096U){fault(w,I9100_AP_ARGUMENT);return I9100_AP_ARGUMENT;}
   /* Publish the actual prepared PCM profile before START. A user client
    * must not have to start DMA just to learn whether prepare succeeded. */
   w->s.last_board=bs;w->s.last_pcm=pcm;w->s.last_board_valid=w->s.last_pcm_valid=1;
   if(w->s.storage){IOFree(w->s.storage,w->s.storage_bytes);w->s.storage=0;}w->s.scratch=0;memset(&w->s.ring,0,sizeof(w->s.ring));
   w->s.period_frames=pcm.period_frames;w->s.storage_bytes=pcm.period_frames*4U*9U;
   w->s.storage=(int16_t *)IOMalloc(w->s.storage_bytes);if(!w->s.storage){fault(w,I9100_AP_IO);return I9100_AP_IO;}
   w->s.scratch=w->s.storage+w->s.period_frames*8U*2U;memset(&w->s.ring,0,sizeof(w->s.ring));
   e=i9100_pcm_ring_init(&w->s.ring,w->s.storage,w->s.period_frames*8U);if(e)fault(w,e);return e;}
 case QUEUE:
  if(!w->s.session_owned||w->s.stopping||w->s.fault)return I9100_AP_DISABLED;
  if(w->s.output_prepared&&!w->s.output_active)return I9100_PCM_BUSY;
  e=i9100_audio_board_worker_queue(&w->s.board_lease,(const int16_t *)arg0,(uint32_t)(uintptr_t)arg1);
  if(e&&e!=I9100_PCM_BUSY&&e!=I9100_PCM_ARGUMENT)fault(w,e);return e;
 case START:
  if(w->s.running||w->s.stopping||w->s.fault||!w->s.session_owned)return I9100_AP_BUSY;
  e=feed(w,0);if(e){fault(w,e);return e;}
  e=i9100_audio_board_worker_start(&w->s.board_lease);capture_pcm(w);if(e){fault(w,e);return e;}
  saved=enter(w);w->s.running=1;leave(w,saved);w->s.event->enable();w->s.timer->enable();
  if(w->s.irq.active){e=i9100_irq_unmask(&w->s.irq);if(e){fault(w,e);return e;}}
  if(w->s.timer->setTimeoutUS(w->s.poll_us)!=kIOReturnSuccess){fault(w,I9100_AP_IO);return I9100_AP_IO;}return 0;
 case STOP:
  e=stop(w);if(e){saved=enter(w);if(!w->s.fault)w->s.error=e;w->s.fault=1;leave(w,saved);}return e;
 case STATUS:{
  struct i9100_audio_worker_status value;memset(&value,0,sizeof(value));saved=enter(w);
  value.running=w->s.running;value.fault=w->s.fault;value.irq_wakes=w->s.irq_wakes;value.pending=w->s.pending;value.error=w->s.error;leave(w,saved);
  value.completed_periods=w->s.completions;value.timer_wakes=w->s.timer_wakes;value.board_held=w->s.board_held;value.session_owned=w->s.session_owned;
  value.last_pcm=w->s.last_pcm;value.last_pcm_valid=w->s.last_pcm_valid;
  value.staged_frames=w->s.ring.frames;value.staging_capacity=w->s.ring.capacity;
  value.enqueued_frames=w->s.ring.written;value.staged_consumed=w->s.ring.consumed;
  value.output_requested=w->s.output_requested;value.output_prepared=w->s.output_prepared;value.output_active=w->s.output_active;
  value.closed=w->s.closed;value.detaching=w->s.detaching;value.cleanup_error=w->s.cleanup_error;value.irq_error=w->s.irq_error;
  value.gated_dma=w->s.gated;
  value.last_board=w->s.last_board;value.last_board_valid=w->s.last_board_valid;
  *(struct i9100_audio_worker_status *)arg0=value;return 0;}
 case DETACH:
  if(w->s.closed)return 0;
  w->s.detaching=1;e=stop(w);if(e){w->s.cleanup_error=e;return e;}
  if(w->s.irq.active){e=i9100_irq_unregister(&w->s.irq);if(e){w->s.cleanup_error=e;return e;}}
  if(w->s.router_held){e=i9100_irq_router_detach(w->s.registry);if(e){w->s.cleanup_error=e;return e;}w->s.router_held=0;}
  if(w->s.added&2U){e=w->s.loop->removeEventSource(w->s.timer);if(e){w->s.cleanup_error=e;return e;}w->s.added&=~2U;}
  if(w->s.added&1U){e=w->s.loop->removeEventSource(w->s.event);if(e){w->s.cleanup_error=e;return e;}w->s.added&=~1U;}
  if(w->s.board_held){e=i9100_audio_board_worker_release(&w->s.board_lease);if(e){w->s.cleanup_error=e;return e;}w->s.board_held=0;}
  w->s.board=0;w->s.closed=1;return 0;
 case ENQUEUE:
  if(!w->s.storage||!w->s.session_owned||w->s.stopping||w->s.fault)return I9100_AP_DISABLED;
  e=i9100_pcm_ring_write(&w->s.ring,(const int16_t *)arg0,(uint32_t)(uintptr_t)arg1);
  if(e)return e;{int queued=feed(w,0);if(queued)fault(w,queued);}return 0;
 case FLUSH:
  w->s.flush_pending=1;
  if(!w->s.storage||!w->s.session_owned||w->s.stopping||w->s.fault)return I9100_AP_DISABLED;
  e=feed(w,1);if(e)fault(w,e);return e;
 case OUTPUT_PREPARE:{
  if(!w->s.session_owned||w->s.running||w->s.stopping||w->s.fault||w->s.output_requested)return I9100_AP_BUSY;
  uint8_t code=0;int32_t actual=0;uint32_t kind=(uint32_t)(uintptr_t)arg0;int32_t gain=*(const int32_t *)arg1;
  if(kind!=I9100_MC_HEADPHONES&&kind!=I9100_MC_SPEAKER)return I9100_MC_ARGUMENT;
  e=i9100_mc1n2_safe_gain(gain,&code,&actual);if(e)return e;
  struct i9100_audio_board_status bs;i9100_pcm_status pcm;e=i9100_audio_board_status(w->s.board,&bs,&pcm);if(e)return e;
  if(pcm.ready_mask||pcm.started)return I9100_PCM_BUSY;
  w->s.last_board=bs;w->s.last_board_valid=1;w->s.last_pcm=pcm;w->s.last_pcm_valid=1;
  w->s.output_requested=1;
  e=i9100_audio_board_worker_output_prepare(1,&w->s.board_lease,(enum i9100_mc1n2_output)kind,gain);
  capture_pcm(w);if(e){fault(w,e);return e;}w->s.output_prepared=1;w->s.output_active=0;return 0;}
 case OUTPUT_ACTIVATE:
  if(!w->s.running||!w->s.output_prepared||w->s.output_active||w->s.stopping||w->s.fault)return I9100_AP_BUSY;
  e=i9100_audio_board_worker_output_activate(&w->s.board_lease);
  {int fresh=capture_pcm(w);
  if(e){
   if((e!=I9100_MC_NOT_READY&&e!=I9100_MC_BUSY)||fresh||w->s.last_board.codec_fault)fault(w,e);
   return e;}}
  w->s.output_active=1;e=feed(w,0);if(e)fault(w,e);return e;
 case OUTPUT_MUTE:
  if(!w->s.output_prepared||w->s.stopping||w->s.fault)return I9100_AP_DISABLED;
  e=i9100_audio_board_worker_output_mute(&w->s.board_lease);capture_pcm(w);
  if(e){fault(w,e);return e;}w->s.output_active=0;return 0;
 default:return I9100_AP_ARGUMENT;
 }
}
void i9100_audio_worker::free()
{
 assert(!s.added&&!s.board_held&&!s.session_owned&&!s.board_lease.owned&&!s.irq.active&&!s.router_held);
 if(s.event)s.event->release();if(s.timer)s.timer->release();if(s.loop)s.loop->release();
 if(s.storage)IOFree(s.storage,s.storage_bytes);
 if(s.guard)IOSimpleLockFree(s.guard);OSObject::free();
}
static void dispose(i9100_audio_worker *w){w->release();}
int i9100_audio_worker_create(int enabled,i9100_audio_board_native *board,i9100_irq_registry *registry,
 int use_irq,int hook,uint32_t poll_us,i9100_audio_worker **out)
{
 if(!out)return I9100_AP_ARGUMENT;if(*out)return I9100_AP_BUSY;if(enabled!=1)return I9100_AP_DISABLED;
 if(!board||!ml_get_interrupts_enabled()||(use_irq!=0&&use_irq!=1)||poll_us<50U||poll_us>2000U||
    (use_irq&&(!registry||hook!=1)))return I9100_AP_ARGUMENT;
 uint32_t gated=0;int mode=i9100_audio_board_gated_enabled(board,&gated);if(mode)return mode;
 if(gated&&use_irq)return I9100_AP_ARGUMENT; /* independent ring native transport currently polls with GIC masked */
 auto *w=new i9100_audio_worker;if(!w)return I9100_AP_IO;
 if(!w->initialize()){w->release();return I9100_AP_IO;}w->s.board=board;w->s.poll_us=poll_us;w->s.gated=gated;w->s.guard=IOSimpleLockAlloc();
 if(!w->s.guard){dispose(w);return I9100_AP_IO;}
 int e=i9100_audio_board_worker_claim(board,&w->s.board_lease);if(e){dispose(w);return e;}w->s.board_held=1;
 w->s.loop=IOWorkLoop::workLoop();w->s.event=new I9100AudioEvents;
 if(!w->s.loop||!w->s.event||!w->s.event->configure(w))e=I9100_AP_IO;
 if(!e){w->s.timer=IOTimerEventSource::timerEventSource((OSObject *)w,timer_action);if(!w->s.timer)e=I9100_AP_IO;}
 if(!e){e=w->s.loop->addEventSource(w->s.event);if(!e)w->s.added|=1U;}
 if(!e){e=w->s.loop->addEventSource(w->s.timer);if(!e)w->s.added|=2U;}
 if(!e){w->s.event->disable();w->s.timer->disable();if(use_irq)e=i9100_irq_register_masked(registry,67,0,interrupt,w,&w->s.irq);}
 if(!e&&use_irq){e=i9100_irq_router_attach(registry);if(!e){w->s.registry=registry;w->s.router_held=1;}}
 if(e){
  int off=0;if(w->s.loop&&w->s.event&&w->s.timer)off=w->s.loop->runAction(control,(OSObject *)w,(void *)DETACH);
  else if(w->s.board_held){off=i9100_audio_board_worker_release(&w->s.board_lease);if(!off)w->s.board_held=0;}
  if(off){w->s.error=e;w->s.cleanup_error=off;w->s.fault=1;*out=w;}else dispose(w);return e;
 }
 *out=w;return 0;
}
static int run(i9100_audio_worker *w,unsigned op,void *arg0=0,void *arg1=0)
{if(!w||w->s.self!=(uintptr_t)w||!w->s.loop||!w->s.event||!w->s.timer||!ml_get_interrupts_enabled())return I9100_AP_ARGUMENT;
 return w->s.loop->runAction(control,(OSObject *)w,(void *)(uintptr_t)op,arg0,arg1);}
int i9100_audio_worker_prepare(i9100_audio_worker *w,uint32_t bclk){return run(w,PREPARE,(void *)(uintptr_t)bclk);}
int i9100_audio_worker_queue(i9100_audio_worker *w,const int16_t *samples,uint32_t frames){return run(w,QUEUE,(void *)samples,(void *)(uintptr_t)frames);}
int i9100_audio_worker_enqueue(i9100_audio_worker *w,const int16_t *samples,uint32_t frames){return run(w,ENQUEUE,(void *)samples,(void *)(uintptr_t)frames);}
int i9100_audio_worker_flush(i9100_audio_worker *w){return run(w,FLUSH);}
int i9100_audio_worker_start(i9100_audio_worker *w){return run(w,START);}
int i9100_audio_worker_output_prepare(int enabled,i9100_audio_worker *w,enum i9100_mc1n2_output kind,int32_t gain)
{return enabled!=1?I9100_MC_DISABLED:run(w,OUTPUT_PREPARE,(void *)(uintptr_t)kind,&gain);}
int i9100_audio_worker_output_activate(i9100_audio_worker *w){return run(w,OUTPUT_ACTIVATE);}
int i9100_audio_worker_output_mute(i9100_audio_worker *w){return run(w,OUTPUT_MUTE);}
int i9100_audio_worker_stop(i9100_audio_worker *w){return run(w,STOP);}
int i9100_audio_worker_status(i9100_audio_worker *w,struct i9100_audio_worker_status *out){return out?run(w,STATUS,out):I9100_AP_ARGUMENT;}
int i9100_audio_worker_destroy(i9100_audio_worker **owner)
{
 if(!owner||!*owner)return I9100_AP_ARGUMENT;i9100_audio_worker *w=*owner;
 if(!ml_get_interrupts_enabled()||(w->s.loop&&w->s.loop->onThread()))return I9100_AP_BUSY;
 int e=0;
 if(w->s.loop&&w->s.event&&w->s.timer)e=run(w,DETACH);
 else if(w->s.board_held){e=i9100_audio_board_worker_release(&w->s.board_lease);if(!e)w->s.board_held=0;}
 if(!e){dispose(w);*owner=0;}return e;
}
