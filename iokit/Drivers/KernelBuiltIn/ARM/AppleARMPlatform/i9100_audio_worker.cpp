/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_worker.h"
#include "i9100_irq_router.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <string.h>
struct i9100_audio_worker;
static void process(i9100_audio_worker *);
class I9100AudioEvents : public IOEventSource {
 OSDeclareDefaultStructors(I9100AudioEvents)
 i9100_audio_worker *client;
public:
 bool configure(i9100_audio_worker *w){client=w;return IOEventSource::init((OSObject *)w,0);}
 void wake(){signalWorkAvailable();}
 virtual bool checkForWork();
};
OSDefineMetaClassAndStructors(I9100AudioEvents,IOEventSource)
struct i9100_audio_worker {
 i9100_audio_board_native *board;IOWorkLoop *loop;I9100AudioEvents *event;IOTimerEventSource *timer;
 IOSimpleLock *guard;i9100_irq_lease irq;
 uint64_t completions;
 uint32_t running,fault,stopping,pending,irq_wakes,timer_wakes,poll_us,added,board_held,session_owned;
 i9100_irq_registry *registry;uint32_t router_held;
 int32_t error;
};
static IOInterruptState enter(i9100_audio_worker *w){return IOSimpleLockLockDisableInterrupt(w->guard);}
static void leave(i9100_audio_worker *w,IOInterruptState s){IOSimpleLockUnlockEnableInterrupt(w->guard,s);}
bool I9100AudioEvents::checkForWork()
{
 IOInterruptState s=enter(client);bool ready=enabled&&client->pending&&client->running&&!client->stopping;
 client->pending=0;leave(client,s);if(ready)process(client);return false;
}
static void fault(i9100_audio_worker *w,int e)
{
 IOInterruptState s=enter(w);w->fault=1;w->error=e;w->running=0;leave(w,s);
 w->timer->disable();w->timer->cancelTimeout();if(w->irq.active)(void)i9100_irq_mask(&w->irq);
}
static void process(i9100_audio_worker *w)
{
 IOInterruptState s=enter(w);bool run=w->running&&!w->stopping&&!w->fault;leave(w,s);if(!run)return;
 uint32_t completed=0;int e=i9100_audio_board_service(w->board,&completed);
 if(e&&e!=I9100_PCM_BUSY){fault(w,e);return;}
 if(completed){w->completions+=completed;if(w->irq.active){e=i9100_irq_unmask(&w->irq);if(e){fault(w,e);return;}}}
 if(w->timer->setTimeoutUS(w->poll_us)!=kIOReturnSuccess)fault(w,I9100_AP_IO);
}
static void timer_action(OSObject *owner,IOTimerEventSource *)
{i9100_audio_worker *w=(i9100_audio_worker *)owner;w->timer_wakes++;process(w);}
static int interrupt(void *cookie,uint32_t ack)
{
 i9100_audio_worker *w=(i9100_audio_worker *)cookie;if((ack&1023U)!=67U)return I9100_IRQ_ARGUMENT;
 int e=i9100_irq_mask(&w->irq);if(e)return e;
 IOInterruptState s=enter(w);bool wake=w->running&&!w->stopping&&!w->fault;
 if(w->irq_wakes!=0xffffffffU)w->irq_wakes++;if(wake)w->pending=1;leave(w,s);
 if(wake)w->event->wake();return 0;
}
enum {PREPARE=1,QUEUE,START,STOP,STATUS,DETACH};
static int stop(i9100_audio_worker *w)
{
 IOInterruptState s=enter(w);w->stopping=1;w->running=0;w->pending=0;leave(w,s);
 w->event->disable();w->timer->disable();w->timer->cancelTimeout();
 int e=w->irq.active?i9100_irq_mask(&w->irq):0;if(e)return e;
 if(w->session_owned){e=i9100_audio_board_pcm_close(w->board);if(e)return e;w->session_owned=0;}
 s=enter(w);w->fault=w->stopping=0;leave(w,s);return 0;
}
static IOReturn control(OSObject *target,void *operation,void *arg0,void *arg1,void *)
{
 i9100_audio_worker *w=(i9100_audio_worker *)target;int e=0;IOInterruptState saved;
 switch((uintptr_t)operation){
 case PREPARE:
  if(w->running||w->stopping||w->fault||w->session_owned)return I9100_AP_BUSY;
  e=i9100_audio_board_pcm_open(w->board,(uint32_t)(uintptr_t)arg0);
  if(!e)w->session_owned=1;else w->error=e;return e;
 case QUEUE:return i9100_audio_board_queue(w->board,(const int16_t *)arg0,(uint32_t)(uintptr_t)arg1);
 case START:
  if(w->running||w->stopping||w->fault)return I9100_AP_BUSY;
  w->session_owned=1;e=i9100_audio_board_start(w->board);if(e){w->error=e;return e;}
  saved=enter(w);w->running=1;leave(w,saved);w->event->enable();w->timer->enable();
  if(w->irq.active){e=i9100_irq_unmask(&w->irq);if(e){fault(w,e);return e;}}
  if(w->timer->setTimeoutUS(w->poll_us)!=kIOReturnSuccess){fault(w,I9100_AP_IO);return I9100_AP_IO;}return 0;
 case STOP:e=stop(w);if(e)w->error=e;return e;
 case STATUS:{
  struct i9100_audio_worker_status value;memset(&value,0,sizeof(value));saved=enter(w);
  value.running=w->running;value.fault=w->fault;value.irq_wakes=w->irq_wakes;value.pending=w->pending;value.error=w->error;leave(w,saved);
  value.completed_periods=w->completions;value.timer_wakes=w->timer_wakes;value.board_held=w->board_held;value.session_owned=w->session_owned;
  *(struct i9100_audio_worker_status *)arg0=value;return 0;}
 case DETACH:
  e=stop(w);if(e)return e;
  if(w->irq.active){e=i9100_irq_unregister(&w->irq);if(e)return e;}
  if(w->router_held){e=i9100_irq_router_detach(w->registry);if(e)return e;w->router_held=0;}
  if(w->added&2U){e=w->loop->removeEventSource(w->timer);if(e)return e;w->added&=~2U;}
  if(w->added&1U){e=w->loop->removeEventSource(w->event);if(e)return e;w->added&=~1U;}
  if(w->board_held){e=i9100_audio_board_unhold(w->board);if(e)return e;w->board_held=0;}return 0;
 default:return I9100_AP_ARGUMENT;
 }
}
static void dispose(i9100_audio_worker *w)
{
 if(w->event)w->event->release();if(w->timer)w->timer->release();if(w->loop)w->loop->release();
 if(w->guard)IOSimpleLockFree(w->guard);IOFree(w,sizeof(*w));
}
int i9100_audio_worker_create(int enabled,i9100_audio_board_native *board,i9100_irq_registry *registry,
 int use_irq,int hook,uint32_t poll_us,i9100_audio_worker **out)
{
 if(!out)return I9100_AP_ARGUMENT;if(*out)return I9100_AP_BUSY;if(enabled!=1)return I9100_AP_DISABLED;
 if(!board||!ml_get_interrupts_enabled()||(use_irq!=0&&use_irq!=1)||poll_us<50U||poll_us>2000U||
    (use_irq&&(!registry||hook!=1)))return I9100_AP_ARGUMENT;
 i9100_audio_worker *w=(i9100_audio_worker *)IOMalloc(sizeof(*w));if(!w)return I9100_AP_IO;
 memset(w,0,sizeof(*w));w->board=board;w->poll_us=poll_us;w->guard=IOSimpleLockAlloc();
 if(!w->guard){dispose(w);return I9100_AP_IO;}
 int e=i9100_audio_board_hold(board);if(e){dispose(w);return e;}w->board_held=1;
 w->loop=IOWorkLoop::workLoop();w->event=new I9100AudioEvents;
 if(!w->loop||!w->event||!w->event->configure(w))e=I9100_AP_IO;
 if(!e){w->timer=IOTimerEventSource::timerEventSource((OSObject *)w,timer_action);if(!w->timer)e=I9100_AP_IO;}
 if(!e){e=w->loop->addEventSource(w->event);if(!e)w->added|=1U;}
 if(!e){e=w->loop->addEventSource(w->timer);if(!e)w->added|=2U;}
 if(!e){w->event->disable();w->timer->disable();if(use_irq)e=i9100_irq_register_masked(registry,67,0,interrupt,w,&w->irq);}
 if(!e&&use_irq){e=i9100_irq_router_attach(registry);if(!e){w->registry=registry;w->router_held=1;}}
 if(e){
  int off=0;if(w->loop&&w->event&&w->timer)off=w->loop->runAction(control,(OSObject *)w,(void *)DETACH);
  else if(w->board_held){off=i9100_audio_board_unhold(board);if(!off)w->board_held=0;}
  if(off)*out=w;else dispose(w);return e;
 }
 *out=w;return 0;
}
static int run(i9100_audio_worker *w,unsigned op,void *arg0=0,void *arg1=0)
{if(!w||!w->loop||!w->event||!w->timer||!ml_get_interrupts_enabled())return I9100_AP_ARGUMENT;
 return w->loop->runAction(control,(OSObject *)w,(void *)(uintptr_t)op,arg0,arg1);}
int i9100_audio_worker_prepare(i9100_audio_worker *w,uint32_t bclk){return run(w,PREPARE,(void *)(uintptr_t)bclk);}
int i9100_audio_worker_queue(i9100_audio_worker *w,const int16_t *samples,uint32_t frames){return run(w,QUEUE,(void *)samples,(void *)(uintptr_t)frames);}
int i9100_audio_worker_start(i9100_audio_worker *w){return run(w,START);}
int i9100_audio_worker_stop(i9100_audio_worker *w){return run(w,STOP);}
int i9100_audio_worker_status(i9100_audio_worker *w,struct i9100_audio_worker_status *out){return out?run(w,STATUS,out):I9100_AP_ARGUMENT;}
int i9100_audio_worker_destroy(i9100_audio_worker **owner)
{
 if(!owner||!*owner)return I9100_AP_ARGUMENT;i9100_audio_worker *w=*owner;
 if(!ml_get_interrupts_enabled()||(w->loop&&w->loop->onThread()))return I9100_AP_BUSY;
 int e=0;
 if(w->loop&&w->event&&w->timer)e=run(w,DETACH);
 else if(w->board_held){e=i9100_audio_board_unhold(w->board);if(!e)w->board_held=0;}
 if(!e){dispose(w);*owner=0;}return e;
}
