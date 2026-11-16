/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_worker.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
class I9100BCMEvents;
struct worker_state {
 IOWorkLoop *loop;I9100BCMEvents *event;IOTimerEventSource *timer;IOLock *control_lock,*pump_lock;IOSimpleLock *guard;
 i9100_bcm_boot *boot;i9100_native_transport_hold *session,hold;i9100_bcm_output output;
 i9100_wlan_control control;i9100_bcm_pump pump;
 uint32_t added,ready,running,stopping,fault,pending,timer_wakes,notifier_wakes,notifiers,poll_us,busy_seen;
 uint64_t busy_started;int32_t error,cleanup_error;
 i9100_bcm_irq_ops irq;i9100_bcm_notify *irq_token;
};
struct i9100_bcm_worker : public OSObject {
 OSDeclareDefaultStructors(i9100_bcm_worker)
public:
 worker_state s;
 bool initialize(){memset(&s,0,sizeof(s));return OSObject::init();}
 virtual void free();
};
OSDefineMetaClassAndStructors(i9100_bcm_worker,OSObject)
static void process(i9100_bcm_worker *);
class I9100BCMEvents : public IOEventSource {
 OSDeclareDefaultStructors(I9100BCMEvents)
 i9100_bcm_worker *client;
public:
 bool configure(i9100_bcm_worker *w){client=w;return IOEventSource::init(w,0);}
 void wake(){signalWorkAvailable();}
 virtual bool checkForWork();
};
OSDefineMetaClassAndStructors(I9100BCMEvents,IOEventSource)
static uint64_t now_ns(void *){uint64_t n=0;absolutetime_to_nanoseconds(mach_absolute_time(),&n);return n;}
static IOInterruptState enter(i9100_bcm_worker *w){return IOSimpleLockLockDisableInterrupt(w->s.guard);}
static void leave(i9100_bcm_worker *w,IOInterruptState state){IOSimpleLockUnlockEnableInterrupt(w->s.guard,state);}
static void inc(uint32_t *p){if(*p!=0xffffffffU)(*p)++;}
static bool separate(i9100_bcm_worker *w,const void *p,size_t bytes)
{uintptr_t a=(uintptr_t)w,b=(uintptr_t)p;return w&&p&&a<=~(uintptr_t)0-sizeof(*w)&&b<=~(uintptr_t)0-bytes&&!(a+sizeof(*w)>b&&b+bytes>a);}
static int control_lock(void *cookie){i9100_bcm_worker *w=(i9100_bcm_worker *)cookie;return !ml_get_interrupts_enabled()||!IOLockTryLock(w->s.control_lock)?-1:0;}
static void control_unlock(void *cookie){IOLockUnlock(((i9100_bcm_worker *)cookie)->s.control_lock);}
static int pump_lock(void *cookie){i9100_bcm_worker *w=(i9100_bcm_worker *)cookie;return !ml_get_interrupts_enabled()||!IOLockTryLock(w->s.pump_lock)?-1:0;}
static void pump_unlock(void *cookie){IOLockUnlock(((i9100_bcm_worker *)cookie)->s.pump_lock);}
static int ethernet(void *cookie,const uint8_t *p,unsigned bytes,unsigned priority)
{i9100_bcm_worker *w=(i9100_bcm_worker *)cookie;return w->s.output.ethernet(w->s.output.cookie,p,bytes,priority);}
static void stop_sources(i9100_bcm_worker *w)
{
 IOInterruptState state=enter(w);w->s.running=0;w->s.pending=0;leave(w,state);
 if(w->s.event)w->s.event->disable();if(w->s.timer){w->s.timer->disable();w->s.timer->cancelTimeout();}
 if(w->s.irq.cookie){int e=w->s.irq.mask(w->s.irq.cookie);if(e)w->s.cleanup_error=e;}
}
static void fault(i9100_bcm_worker *w,int error)
{
 IOInterruptState state=enter(w);w->s.fault=1;w->s.ready=0;w->s.error=error;leave(w,state);stop_sources(w);
 if(w->s.pump.initialized&&!w->s.pump.fault)(void)i9100_bcm_pump_abort(&w->s.pump);
}
static int held(i9100_bcm_worker *w)
{
 int e=i9100_peripheral_native_transport_hold_check(w->s.session);
 return e?e:i9100_peripheral_native_transport_hold_check(&w->s.hold);
}
static void process(i9100_bcm_worker *w)
{
 IOInterruptState state=enter(w);bool run=w->s.running&&!w->s.stopping&&!w->s.fault;leave(w,state);if(!run)return;
 int e=held(w);if(!e)e=i9100_bcm_pump_tick(&w->s.pump,4U);
 if(e==I9100_GPIO_BUSY||e==I9100_PUMP_BUSY){
  uint64_t now=now_ns(0);if(!w->s.busy_seen){w->s.busy_seen=1;w->s.busy_started=now;}
  else if(now<w->s.busy_started||now-w->s.busy_started>500000000ULL){fault(w,I9100_WLAN_TIMEOUT);return;}
 }else if(e){fault(w,e);return;}else{
  w->s.busy_seen=0;
  if(w->s.irq.cookie){
   if(w->s.pump.pending_rx)e=w->s.irq.mask(w->s.irq.cookie);
   else{int armed=w->s.irq.rearm(w->s.irq.cookie);if(armed<0)e=armed;
    else if(!armed){unsigned work=0;e=i9100_bcm_pump_recheck(&w->s.pump,&work);
     if(e||work){int masked=w->s.irq.mask(w->s.irq.cookie);if(masked)e=masked;
      if(work){state=enter(w);w->s.pending=1;leave(w,state);w->s.event->wake();}
     }
    }
   }
   if(e&&e!=I9100_PUMP_BUSY){fault(w,e);return;}
  }
 }
 if(w->s.timer->setTimeoutUS(w->s.poll_us)!=kIOReturnSuccess)fault(w,I9100_PUMP_IO);
}
bool I9100BCMEvents::checkForWork()
{if(!enabled)return false;IOInterruptState state=enter(client);bool run=client->s.pending&&client->s.running&&!client->s.stopping;client->s.pending=0;leave(client,state);if(run)process(client);return false;}
static void timer(OSObject *owner,IOTimerEventSource *)
{i9100_bcm_worker *w=(i9100_bcm_worker *)owner;inc(&w->s.timer_wakes);process(w);}
enum {START=1,JOIN,SEND,DISCONNECT,ABORT,STATUS,DETACH,NOTIFIER_GET,NOTIFIER_PUT,IRQ_ATTACH,IRQ_DETACH};
struct send_args {const uint8_t *bytes;unsigned count,priority;};
struct irq_args {const i9100_bcm_irq_ops *ops;i9100_bcm_notify *token;};
static int detach(i9100_bcm_worker *w)
{
 IOInterruptState state=enter(w);bool notify=w->s.notifiers!=0;leave(w,state);if(notify||w->s.irq.cookie)return I9100_PUMP_BUSY;
 if(w->s.control.initialized&&w->s.control.state!=I9100_WLAN_IDLE&&w->s.control.state!=I9100_WLAN_FAULT)return I9100_PUMP_BUSY;
 w->s.stopping=1;w->s.ready=0;stop_sources(w);int e=0;
 if(w->s.added&2U){e=w->s.loop->removeEventSource(w->s.timer);if(e)return e;w->s.added&=~2U;}
 if(w->s.added&1U){e=w->s.loop->removeEventSource(w->s.event);if(e)return e;w->s.added&=~1U;}
 if(w->s.pump.initialized){e=i9100_bcm_pump_close(&w->s.pump);if(e)return e;}
 if(w->s.control.initialized){e=i9100_wlan_control_close(&w->s.control);if(e)return e;}
 if(w->s.hold.owned){e=i9100_peripheral_native_transport_hold_put(&w->s.hold);if(e)return e;}
 w->s.ready=0;return 0;
}
static IOReturn action(OSObject *target,void *operation,void *arg,void *,void *)
{
 i9100_bcm_worker *w=(i9100_bcm_worker *)target;IOInterruptState state;int e=0;
 switch((uintptr_t)operation){
 case START:
  if(!w->s.ready||w->s.running||w->s.stopping||w->s.fault)return I9100_PUMP_BUSY;
  e=held(w);if(e){if(e!=I9100_GPIO_BUSY)fault(w,e);return e;}state=enter(w);w->s.running=1;leave(w,state);w->s.event->enable();w->s.timer->enable();
  if(w->s.timer->setTimeoutUS(w->s.poll_us)!=kIOReturnSuccess){fault(w,I9100_PUMP_IO);return I9100_PUMP_IO;}return 0;
 case JOIN:case SEND:case DISCONNECT:
  if(!w->s.ready||!w->s.running||w->s.stopping||w->s.fault)return I9100_PUMP_DISABLED;
  e=held(w);if(e){if(e!=I9100_GPIO_BUSY)fault(w,e);return e;}
  if((uintptr_t)operation==JOIN)e=i9100_wlan_join(&w->s.control,(const i9100_wlan_config *)arg);
  else if((uintptr_t)operation==DISCONNECT)e=i9100_wlan_disconnect(&w->s.control);
  else{send_args *p=(send_args *)arg;e=i9100_wlan_send(&w->s.control,p->bytes,p->count,p->priority);}
  if(!e){state=enter(w);w->s.pending=1;leave(w,state);w->s.event->wake();}return e;
 case ABORT:if(!w->s.pump.initialized)return I9100_PUMP_DISABLED;e=i9100_bcm_pump_abort(&w->s.pump);fault(w,e);return e;
 case STATUS:{struct i9100_bcm_worker_status value;memset(&value,0,sizeof(value));state=enter(w);
  value.running=w->s.running;value.fault=w->s.fault;value.pending=w->s.pending;value.notifier_wakes=w->s.notifier_wakes;value.notifiers=w->s.notifiers;leave(w,state);
  value.ready=w->s.ready;value.stopping=w->s.stopping;value.timer_wakes=w->s.timer_wakes;value.worker_hold=w->s.hold.owned;
  value.control_state=w->s.control.state;value.error=w->s.error;value.cleanup_error=w->s.cleanup_error;
  value.rx_frames=w->s.pump.rx_frames;value.data_frames=w->s.pump.data_frames;value.dropped_frames=w->s.pump.dropped_frames;
  value.empty_reads=w->s.pump.empty_reads;value.budget_yields=w->s.pump.budget_yields;value.interrupt_status=w->s.pump.interrupt_status;value.mailbox=w->s.pump.mailbox;
  *(struct i9100_bcm_worker_status *)arg=value;return 0;}
 case DETACH:e=detach(w);w->s.cleanup_error=e;return e;
 case NOTIFIER_GET:{i9100_bcm_notify *p=(i9100_bcm_notify *)arg;
  if(!separate(w,p,sizeof(*p))||p->owned||(p->self&&p->self!=(uintptr_t)p)||!w->s.ready||w->s.stopping||w->s.fault)return I9100_PUMP_ARGUMENT;
  state=enter(w);if(w->s.notifiers==0xffffffffU)e=I9100_PUMP_BUSY;
  else{w->s.notifiers++;memset(p,0,sizeof(*p));p->worker=w;p->self=(uintptr_t)p;p->owned=1;w->retain();}leave(w,state);return e;}
 case NOTIFIER_PUT:{i9100_bcm_notify *p=(i9100_bcm_notify *)arg;state=enter(w);
  if(!p||!p->owned||p->worker!=w||p->self!=(uintptr_t)p||!w->s.notifiers)e=I9100_PUMP_ARGUMENT;
  else if(p->busy||(w->s.irq.cookie&&w->s.irq_token==p))e=I9100_PUMP_BUSY;else{w->s.notifiers--;p->owned=0;}leave(w,state);return e;}
 case IRQ_ATTACH:{irq_args *p=(irq_args *)arg;
  if(!p||!p->ops||!p->ops->cookie||!p->ops->rearm||!p->ops->mask||!p->token||!p->token->owned||
     p->token->worker!=w||p->token->self!=(uintptr_t)p->token||w->s.irq.cookie||!w->s.ready)return I9100_PUMP_ARGUMENT;
  w->s.irq=*p->ops;w->s.irq_token=p->token;return 0;}
 case IRQ_DETACH:
  if(!arg||arg!=w->s.irq.cookie)return I9100_PUMP_ARGUMENT;
  e=w->s.irq.mask(w->s.irq.cookie);if(e)return e;memset(&w->s.irq,0,sizeof(w->s.irq));w->s.irq_token=0;return 0;
 default:return I9100_PUMP_ARGUMENT;
 }
}
void i9100_bcm_worker::free()
{
 if(s.event)s.event->release();if(s.timer)s.timer->release();if(s.loop)s.loop->release();
 if(s.guard)IOSimpleLockFree(s.guard);if(s.control_lock)IOLockFree(s.control_lock);if(s.pump_lock)IOLockFree(s.pump_lock);OSObject::free();
}
int i9100_bcm_worker_create(int enabled,i9100_bcm_boot *boot,i9100_native_transport_hold *session,uint32_t poll_us,const i9100_bcm_output *out,i9100_bcm_worker **owner)
{
 if(!owner)return I9100_PUMP_ARGUMENT;if(*owner)return I9100_PUMP_BUSY;if(enabled!=1)return I9100_PUMP_DISABLED;
 if(!ml_get_interrupts_enabled()||!boot||!boot->ready||!boot->firmware||!boot->firmware->chip||!boot->packets||
    !session||!session->owned||session->kind!=I9100_NATIVE_SDIO||session->lease!=boot->packets->io.cookie||
    !out||!out->ethernet||poll_us<500U||poll_us>10000U)return I9100_PUMP_ARGUMENT;
 int e=i9100_peripheral_native_transport_hold_check(session);if(e)return e;
 i9100_bcm_worker *w=new i9100_bcm_worker;if(!w)return I9100_PUMP_IO;
 if(!w->initialize()){w->release();return I9100_PUMP_IO;}w->s.boot=boot;w->s.session=session;w->s.output=*out;w->s.poll_us=poll_us;
 w->s.control_lock=IOLockAlloc();w->s.pump_lock=IOLockAlloc();w->s.guard=IOSimpleLockAlloc();
 if(!w->s.control_lock||!w->s.pump_lock||!w->s.guard){w->release();return I9100_PUMP_IO;}
 e=i9100_peripheral_native_transport_hold_get(session->lease,&w->s.hold);
 if(!e){w->s.loop=IOWorkLoop::workLoop();w->s.event=new I9100BCMEvents;
  if(!w->s.loop||!w->s.event||!w->s.event->configure(w))e=I9100_PUMP_IO;}
 if(!e){w->s.timer=IOTimerEventSource::timerEventSource(w,timer);if(!w->s.timer)e=I9100_PUMP_IO;}
 if(!e){e=w->s.loop->addEventSource(w->s.event);if(!e)w->s.added|=1U;}
 if(!e){e=w->s.loop->addEventSource(w->s.timer);if(!e)w->s.added|=2U;}
 if(!e){w->s.event->disable();w->s.timer->disable();i9100_wlan_io ci={w,control_lock,control_unlock,now_ns};
  e=i9100_wlan_control_init(&w->s.control,1,boot->firmware->chip->bus,&ci,boot->generation);}
 if(!e){i9100_pump_io pi={w,pump_lock,pump_unlock,ethernet};e=i9100_bcm_pump_init(&w->s.pump,1,boot,&w->s.control,&pi);}
 if(e){w->s.error=e;int cleanup=w->s.loop?w->s.loop->runAction(action,w,(void *)DETACH):detach(w);
  if(cleanup){w->s.cleanup_error=cleanup;*owner=w;}else w->release();return e;}
 w->s.ready=1;*owner=w;return 0;
}
static int run(i9100_bcm_worker *w,unsigned op,void *arg=0)
{return !w||!w->s.loop||!w->s.guard||!ml_get_interrupts_enabled()?I9100_PUMP_ARGUMENT:w->s.loop->runAction(action,w,(void *)(uintptr_t)op,arg);}
int i9100_bcm_worker_start(i9100_bcm_worker *w){return run(w,START);}
int i9100_bcm_worker_join(i9100_bcm_worker *w,const i9100_wlan_config *c){return c?run(w,JOIN,(void *)c):I9100_PUMP_ARGUMENT;}
int i9100_bcm_worker_send(i9100_bcm_worker *w,const uint8_t *p,unsigned bytes,unsigned priority){send_args args={p,bytes,priority};return run(w,SEND,&args);}
int i9100_bcm_worker_disconnect(i9100_bcm_worker *w){return run(w,DISCONNECT);}
int i9100_bcm_worker_abort(i9100_bcm_worker *w){return run(w,ABORT);}
int i9100_bcm_worker_status(i9100_bcm_worker *w,struct i9100_bcm_worker_status *out)
{
 if(!separate(w,out,sizeof(*out))||!ml_get_interrupts_enabled())return I9100_PUMP_ARGUMENT;
 /* No work-loop exists on an early retained construction failure, so no
  * callbacks can race this diagnostic read. Keep it inspectable for cleanup. */
 return w->s.loop?run(w,STATUS,out):action(w,(void *)STATUS,out,0,0);
}
int i9100_bcm_worker_destroy(i9100_bcm_worker **owner)
{
 if(!owner||!*owner)return I9100_PUMP_ARGUMENT;i9100_bcm_worker *w=*owner;
 if(!separate(w,owner,sizeof(*owner)))return I9100_PUMP_ARGUMENT;
 if(!ml_get_interrupts_enabled()||(w->s.loop&&w->s.loop->onThread()))return I9100_PUMP_BUSY;
 int e=w->s.loop?run(w,DETACH):detach(w);if(!e){w->release();*owner=0;}return e;
}
int i9100_bcm_worker_notifier_get(i9100_bcm_worker *w,i9100_bcm_notify *n){return run(w,NOTIFIER_GET,n);}
int i9100_bcm_worker_irq_attach(i9100_bcm_worker *w,const i9100_bcm_irq_ops *ops,i9100_bcm_notify *token)
{irq_args args={ops,token};return run(w,IRQ_ATTACH,&args);}
int i9100_bcm_worker_irq_detach(i9100_bcm_worker *w,void *cookie){return run(w,IRQ_DETACH,cookie);}
int i9100_bcm_worker_notifier_put(i9100_bcm_notify *n)
{if(!n||!n->owned||n->self!=(uintptr_t)n||!n->worker)return I9100_PUMP_ARGUMENT;i9100_bcm_worker *w=n->worker;int e=run(w,NOTIFIER_PUT,n);if(!e)w->release();return e;}
int i9100_bcm_worker_signal(i9100_bcm_notify *n)
{
 if(!n||!n->owned||n->self!=(uintptr_t)n||!n->worker)return I9100_PUMP_ARGUMENT;i9100_bcm_worker *w=n->worker;
 IOInterruptState state=enter(w);if(!n->owned||!w->s.notifiers){leave(w,state);return I9100_PUMP_ARGUMENT;}
 if(n->busy==0xffffffffU){leave(w,state);return I9100_PUMP_BUSY;}
 n->busy++;inc(&w->s.notifier_wakes);bool wake=w->s.running&&!w->s.stopping&&!w->s.fault;
 if(wake)w->s.pending=1;leave(w,state);if(wake)w->s.event->wake();state=enter(w);n->busy--;leave(w,state);return 0;
}
