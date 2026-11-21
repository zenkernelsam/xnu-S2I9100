/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_worker.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOService.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
enum {DEFER_US=2000,CMD_TIMEOUT_NS=500000000U,RETIRE_NS=6000000000ULL,RETIRE_SHUTDOWN_TRIES=64U};
struct mfc_request {
 mfc_request *next;void *owner;uint64_t token;unsigned op,bytes,tag,last;
 i9100_mfc_worker_done done;bool cancelled,finished,waited,timed_out,retire_stuck;int result;
 uint8_t *input,*stream;uint32_t stream_bytes;i9100_mfc_firmware_profile profile;
 uint32_t baseline,defers,shutdown_tries;uint64_t deadline;
};
class I9100MFCLoop : public IOWorkLoop {
 OSDeclareDefaultStructors(I9100MFCLoop)
public:
 bool configure(){return IOWorkLoop::init();}
#ifdef I9100_MFC_WORKER_FACADE
 /* Host model boundary: the facade gate is a real recursive mutex owned by
  * the runner; these drop/reacquire exactly one level on that thread. */
 void callbackGateOpen();void callbackGateClose();
 bool workerOnThread()const;
#else
 void callbackGateOpen(){IOWorkLoop::openGate();}
 void callbackGateClose(){IOWorkLoop::closeGate();}
 bool workerOnThread()const{return IOWorkLoop::onThread();}
#endif
};
class I9100MFCEvents : public IOEventSource {
 OSDeclareDefaultStructors(I9100MFCEvents)
 i9100_mfc_worker *client;
public:
 bool configure(i9100_mfc_worker *);
 void wake(){signalWorkAvailable();}
 bool checkForWork() override;
};
struct i9100_mfc_worker : public OSObject {
 OSDeclareDefaultStructors(i9100_mfc_worker)
public:
 i9100_mfc_native *native;i9100_mfc_board *board;i9100_mfc_irq *irq;void *irq_storage;
 I9100MFCLoop *loop;I9100MFCEvents *event;IOTimerEventSource *timer;IOLock *guard;
 mfc_request *head,*tail,*active,*issuing,*cancelled_head;
 volatile uint32_t irq_pending,timer_pending;
 bool added,timer_added,held,stopping,fault,closed,retained,armed,ctl_bound;
 uintptr_t ctl;unsigned queued,callbacks,cancel_pending;
 uint64_t submitted,completed,cancelled,interrupts,defers,late_irq,timeouts;
 int error,cleanup_error;
 bool initialize(){native=0;board=0;irq=0;irq_storage=0;loop=0;event=0;timer=0;guard=0;head=tail=active=issuing=cancelled_head=0;
  irq_pending=timer_pending=0;added=timer_added=held=stopping=fault=closed=retained=armed=ctl_bound=false;ctl=0;
  queued=callbacks=cancel_pending=0;submitted=completed=cancelled=interrupts=defers=late_irq=timeouts=0;error=cleanup_error=0;
  return OSObject::init();}
 void free() override;
};
OSDefineMetaClassAndStructors(I9100MFCEvents,IOEventSource)
#ifdef I9100_MFC_WORKER_FACADE
extern thread_local IOWorkLoop *i9100_mfc_facade_current_loop;
extern void i9100_mfc_facade_gate_open(IOWorkLoop *),i9100_mfc_facade_gate_close(IOWorkLoop *);
bool I9100MFCLoop::workerOnThread()const{return i9100_mfc_facade_current_loop==this;}
void I9100MFCLoop::callbackGateOpen(){i9100_mfc_facade_gate_open(this);}
void I9100MFCLoop::callbackGateClose(){i9100_mfc_facade_gate_close(this);}
#endif
OSDefineMetaClassAndStructors(I9100MFCLoop,IOWorkLoop)
OSDefineMetaClassAndStructors(i9100_mfc_worker,OSObject)
/* A failed teardown retains the worker itself through a plain OSObject
 * reference: the caller keeps its `w` pointer for the explicit retry, so no
 * global list and no unprotected internal pointer is ever needed. A later
 * successful destroy releases exactly that reference. */
static void preserve(i9100_mfc_worker *w,int e)
{w->cleanup_error=e;if(w->retained)return;w->retained=true;w->retain();}
static void unpreserve(i9100_mfc_worker *w)
{if(!w->retained)return;w->retained=false;w->release();}
static bool thread_context(){return ml_get_interrupts_enabled()&&!ml_at_interrupt_context();}
static void inc(uint64_t *v){if(*v!=~(uint64_t)0)(*v)++;}
static uint64_t now_ns(){uint64_t n=0;absolutetime_to_nanoseconds(mach_absolute_time(),&n);return n;}
static void wake(i9100_mfc_worker *w){IOLockLock(w->guard);if(w->added)w->event->wake();IOLockUnlock(w->guard);}
static void irq_signal(void *p)
{ /* Interrupt-context notify only: mark and signal, nothing else. */
 i9100_mfc_worker *w=(i9100_mfc_worker *)p;__sync_lock_test_and_set(&w->irq_pending,1);
 if(w->added)w->event->wake();
}
static void on_timeout(OSObject *owner,IOTimerEventSource *)
{i9100_mfc_worker *w=(i9100_mfc_worker *)owner;__sync_lock_test_and_set(&w->timer_pending,1);w->event->wake();}
bool I9100MFCEvents::configure(i9100_mfc_worker *w){client=w;return IOEventSource::init(w,0);}
static void free_request(mfc_request *r)
{if(r->input)IOFree(r->input,r->bytes);if(r->stream)IOFree(r->stream,r->stream_bytes);IOFree(r,sizeof(*r));}
/* Board/token APIs report the power-domain error range; contention there is
 * recoverable by a bounded defer, not a request fault. */
static bool gate_busy(int e){return e==I9100_MFC_BUSY||e==I9100_MP_BUSY;}
static int line_arm(i9100_mfc_worker *w)
{
 if(w->armed||!w->irq)return 0;int e=i9100_mfc_irq_arm(w->irq);
 if(!e){IOLockLock(w->guard);w->armed=true;IOLockUnlock(w->guard);}
 else if(gate_busy(e))return e; /* board gate held elsewhere: bounded defer */
 else{IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);}
 return e;
}
static int line_disarm(i9100_mfc_worker *w)
{
 if(!w->irq)return 0;
 if(!w->armed){
  /* Local bookkeeping can lag real line state: a dropped ISR-path mask
   * leaves the slot enabled+faulted while `armed` already cleared at the
   * notify. Consult the lease's own recorded/actual state before skipping
   * the real mask. A failed status query itself means the slot needs the
   * disarm/repair path, so only a proven-quiet line skips the write. */
  struct i9100_mfc_irq_status qs;
  if(!i9100_mfc_irq_status(w->irq,&qs)&&!qs.armed&&!qs.enabled)return 0;
 }
 int e=i9100_mfc_irq_disarm(w->irq);
 if(!e){IOLockLock(w->guard);w->armed=false;IOLockUnlock(w->guard);}
 else if(!gate_busy(e)){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);}
 return e;
}
/* The client callback is delivered exactly once, without retiring the
 * request: the callback reference counts into the bounded ledger while the
 * gate is open, and the request keeps its ledger slot until the hardware
 * side is proven gone. Runs only on the workloop thread. */
static void notify(i9100_mfc_worker *w,mfc_request *r)
{
 IOLockLock(w->guard);w->callbacks++;IOLockUnlock(w->guard);
 w->loop->callbackGateOpen();
 if(r->done)r->done(r->owner,r->token,r->result);
 w->loop->callbackGateClose();
 IOLockLock(w->guard);w->callbacks--;IOLockUnlock(w->guard);
}
/* Real retirement for a request the client already released: the hardware
 * side is proven gone (reply consumed, engine deadline, or a proven owner
 * shutdown), so the internal owner drops the ledger slot and frees the
 * request without a second callback. */
static void retire(i9100_mfc_worker *w,mfc_request *r)
{
 IOLockLock(w->guard);
 if(w->active==r){w->active=0;w->timer->cancelTimeout();}
 if(w->issuing==r)w->issuing=0;
 inc(&w->completed);if(r->cancelled)inc(&w->cancelled);
 IOLockUnlock(w->guard);
 free_request(r);
}
/* Notify the client exactly once, then keep the issued request internally
 * owned and counted as an in-flight retirement until the engine's real
 * reply/deadline/error or a proven owner shutdown retires its hardware
 * side. The retirement bound outlives the maximum firmware deadline. */
static void retire_begin(i9100_mfc_worker *w,mfc_request *r,int result)
{
 if(r->timed_out)return;
 r->result=result;notify(w,r);
 IOLockLock(w->guard);r->timed_out=true;r->defers=0;r->deadline=now_ns()+RETIRE_NS;IOLockUnlock(w->guard);
 w->timer->setTimeoutUS(DEFER_US);
}
/* Runs only on the workloop thread: the gate is open/closed around the
 * callback by the thread that actually owns the recursive lock. */
static void finish(i9100_mfc_worker *w,mfc_request *r)
{
 /* A timed-out request already delivered its callback: only the real
  * retirement remains, and freeing must not precede it. */
 if(r->timed_out){retire(w,r);return;}
 IOLockLock(w->guard);
 if(w->active==r){w->active=0;w->timer->cancelTimeout();}
 if(w->issuing==r)w->issuing=0;
 inc(&w->completed);if(r->cancelled)inc(&w->cancelled);
 w->callbacks++;IOLockUnlock(w->guard);
 if(r->cancelled&&!r->result)r->result=I9100_MFC_ABORTED;
 /* Callback runs with queue lock and workloop gate released so callers may
  * reenter submit/cancel/stop. The callback reference stays counted until
  * the last request access completed and the gate is safely reclosed:
  * decrementing any earlier would let a concurrent destroy/stop read the
  * engine as idle while user code still runs inside it. */
 w->loop->callbackGateOpen();
 if(r->done)r->done(r->owner,r->token,r->result);
 free_request(r);
 w->loop->callbackGateClose();
 IOLockLock(w->guard);w->callbacks--;IOLockUnlock(w->guard);
}
/* 1=pending on a bounded defer, -1=terminal (r->result set), 2=issued
 * command wedged past its retirement bound: ownership and a real fault are
 * latched and only the same owner's explicit cleanup can prove quiescence. */
static int defer(i9100_mfc_worker *w,mfc_request *r)
{
 if(++r->defers>I9100_MFC_WORKER_DEFERS||now_ns()>=r->deadline){
  inc(&w->timeouts);
  if(w->active==r&&!r->timed_out){
   /* The command is already issued to hardware: the client is told its
    * bounded deadline exactly once — ABORTED when a cancel was the real
    * cause — but the request keeps its active slot and real IRQ/poll
    * service as an internal, counted retirement owner until real
    * completion or a proven stop. */
   retire_begin(w,r,r->cancelled?I9100_MFC_ABORTED:I9100_MFC_TIMEOUT);
   return 1;
  }
  if(w->active==r&&!r->retire_stuck){
   /* The retirement bound outlived the firmware's own deadline and the
    * engine still could not retire the command: the exact owner performs
    * the real shutdown now. A proven-quiet engine retires the request; a
    * failed proof keeps controller, arenas, input and a latched fault for
    * the same owner's explicit cleanup. */
   int se=i9100_mfc_native_shutdown_ctl(w->native,w->ctl);r->shutdown_tries++;
   if(!se){
    /* The forced reset ended the command but also dropped the firmware
     * state machine: refuse new issue until explicit lifecycle cleanup. */
    IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=I9100_MFC_TIMEOUT;IOLockUnlock(w->guard);
    r->result=I9100_MFC_TIMEOUT;return -1;
   }
   IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=se;
   if(gate_busy(se)&&r->shutdown_tries<RETIRE_SHUTDOWN_TRIES)
    {IOLockUnlock(w->guard);w->timer->setTimeoutUS(DEFER_US);return 1;}
   r->retire_stuck=true;IOLockUnlock(w->guard);return 2;
  }
  r->result=I9100_MFC_TIMEOUT;return -1;
 }
 w->defers++;w->timer->setTimeoutUS(DEFER_US);return 1;
}
/* Terminal outcome on an issued request: while the mailbox still reports
 * the command pending, the client is told exactly once and the request
 * keeps an internal, capacity-counted retirement owner until the engine or
 * a proven owner shutdown retires the hardware side. A proven-quiet mailbox
 * (or an unreadable one) retires now with -1. */
static int terminal(i9100_mfc_worker *w,mfc_request *r,int e)
{
 struct i9100_mfc_native_status st;int se=i9100_mfc_native_status(w->native,&st);
 if(se==I9100_MFC_BUSY||(!se&&st.pending))
 {retire_begin(w,r,e);w->timer->setTimeoutUS(DEFER_US);return 1;} /* keep the retirement clock live: the next service goes through the bounded deadline/shutdown path */
 r->result=e;return -1;
}
/* Runs on the workloop thread under its gate. Rereads actual mailbox state
 * through the native status/poll path; never trusts the ISR alone.
 * 0=command complete, 1=still pending, 2=wedged retirement, -1=terminal
 * (r->result set). */
static int service_active(i9100_mfc_worker *w,mfc_request *r)
{
 struct i9100_mfc_native_status st;
 int e=i9100_mfc_native_status(w->native,&st);
 if(e==I9100_MFC_BUSY)return defer(w,r); /* engine gate held elsewhere */
 if(e){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return terminal(w,r,e);}
 if(r->timed_out){
  /* The hardware side retired for real: a reply, the firmware's own
   * deadline fault (pending cleared with an error latched), or a proven
   * owner shutdown. Free silently — the client already knows its deadline
   * fired exactly once. */
  if(!st.pending){if(st.error){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=st.error;IOLockUnlock(w->guard);}return 0;}
  /* Shutdown proof already failed under the bounded tries: keep ownership
   * and wait the same owner's explicit cleanup, no self retry storm. */
  if(r->retire_stuck)return 2;
  if(st.error){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=st.error;IOLockUnlock(w->guard);return defer(w,r);}
  /* The poll service must still run: only it consumes a real late reply
   * and only it fires the firmware's own deadline — skipping it would
   * leave the issued command unread until the forced shutdown. */
  e=i9100_mfc_native_poll_ctl(w->native,w->ctl);
  if(e==I9100_MFC_BUSY)return defer(w,r);
  if(e){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return defer(w,r);}
  e=i9100_mfc_native_status(w->native,&st);
  if(e==I9100_MFC_BUSY)return defer(w,r);
  if(e){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return defer(w,r);}
  if(!st.pending){if(st.error){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=st.error;IOLockUnlock(w->guard);}return 0;}
  /* Still pending: keep the bounded retirement clock running so the owner
   * shutdown below fires on schedule — never a stall after the single
   * client notification. */
  return defer(w,r);
 }
 if(st.error){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=st.error;IOLockUnlock(w->guard);return terminal(w,r,st.error);}
 if(r->op==I9100_MFC_WORK_BOOT){
  if(st.phase==I9100_MFC_N_READY)return 0;
  if(st.phase==I9100_MFC_N_FAILED){e=st.error?st.error:I9100_MFC_IO;IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return terminal(w,r,e);}
  e=i9100_mfc_native_poll_ctl(w->native,w->ctl);
  if(e==I9100_MFC_BUSY)return defer(w,r);
  if(e){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return terminal(w,r,e);}
  e=i9100_mfc_native_status(w->native,&st);if(e==I9100_MFC_BUSY)return defer(w,r);
  if(e){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return terminal(w,r,e);}return st.phase==I9100_MFC_N_READY?0:defer(w,r);
 }
 if((st.frame_completed>r->baseline&&!r->last)||(r->last&&st.drained))return 0;
 e=i9100_mfc_native_poll_ctl(w->native,w->ctl);
 if(e==I9100_MFC_BUSY)return defer(w,r);
 /* A terminal engine error is real damage on the owner's path: latch the
  * worker fault too so later submissions are refused by admission, not
  * merely retired one request at a time. */
 if(e){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return terminal(w,r,e);}
 e=i9100_mfc_native_status(w->native,&st);if(e==I9100_MFC_BUSY)return defer(w,r);
 if(e){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);return terminal(w,r,e);}
 if((st.frame_completed>r->baseline&&!r->last)||(r->last&&st.drained))return 0;
 /* LAST: mailbox quiet but EMPTY not reported; issue the real drain
  * continuation instead of fabricating a completion. */
 if(r->last&&!st.pending){
  e=i9100_mfc_native_frame_ctl(w->native,0,0,0,1,w->ctl);
  if(e&&e!=I9100_MFC_BUSY)return terminal(w,r,e);
 }
 return defer(w,r);
}
static bool process(i9100_mfc_worker *w)
{
 /* External cancels park unlinked requests here; they are finished on this
  * thread under the real gate so the caller's context never touches it. */
 for(;;){
  IOLockLock(w->guard);mfc_request *c=w->cancelled_head;
  if(c){w->cancelled_head=c->next;c->next=0;if(w->cancel_pending)w->cancel_pending--;}
  IOLockUnlock(w->guard);
  if(!c)break;
  if(!c->result)c->result=I9100_MFC_ABORTED;c->cancelled=true;finish(w,c);
 }
 bool notified=__sync_lock_test_and_set(&w->irq_pending,0)!=0;
 bool timer_fired=__sync_lock_test_and_set(&w->timer_pending,0)!=0;
 IOLockLock(w->guard);mfc_request *r=w->active;bool stopping=w->stopping;
 /* A delivered notify means the upper half masked the leased line; the local
  * armed bookkeeping must not suppress the re-arm below. */
 if(notified){inc(&w->interrupts);w->armed=false;}
 IOLockUnlock(w->guard);
 if(!r){
  /* Idle drain on every wake, flagged or not: poll first so a live reply
   * can still retire a dead mailbox command (the latch is its interrupt
   * evidence), then ack the residue or the source latch stays raised and
   * quiet() rejects every later command issue. */
  bool late=notified||timer_fired;
  int pe=i9100_mfc_native_poll_ctl(w->native,w->ctl);
  /* Mailbox residue keeps a bounded drain service alive: a command whose
   * worker request already retired can still hold a pending firmware
   * transaction, and only a real reply, poll consumption or the poll's own
   * deadline may retire it — never a fabricated wake. */
  struct i9100_mfc_native_status st;
  int se=i9100_mfc_native_status(w->native,&st);
  if(!se&&st.pending&&!st.error)w->timer->setTimeoutUS(DEFER_US);
  int ae=w->irq?i9100_mfc_irq_ack(w->irq):0;
  if(ae>0)late=true;
  /* Poll, status and ack negatives are real owner-path damage — a lost
   * clock or power domain must never be read as a quiet mailbox. Latch the
   * first one so admission refuses new issue; transient gate contention is
   * the only non-latching outcome. */
  int lost=pe<0&&!gate_busy(pe)?pe:se<0&&!gate_busy(se)?se:ae<0&&!gate_busy(ae)?ae:(!se?st.error:0);
  if(lost){IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=lost;IOLockUnlock(w->guard);}
  if(late)w->late_irq++;}
 if(r&&(notified||timer_fired||stopping||r->cancelled)){
  /* A cancelled-but-issued request keeps its real retirement path: the
   * command stays active and is retired by an actual IRQ notify, polling
   * window or the command deadline / proven reset, never by an early abort
   * while the engine still reports it pending. */
  int s=service_active(w,r);
  if(s!=1&&s!=2)finish(w,r); /* 0 complete / terminal error, exactly once */
  IOLockLock(w->guard);r=w->active;IOLockUnlock(w->guard);
  if(r){int ae=line_arm(w);
   /* A failed arm still leaves the issued command in hardware: poll-only
    * retirement on the same bounded windows, never a detached finish. */
   if(gate_busy(ae)){if(defer(w,r)<0)finish(w,r);}
   else if(ae)retire_begin(w,r,ae);}
 }
 if(!r){
  IOLockLock(w->guard);
  /* The head stays counted as queued until it is either safely in `active`
   * or finished: decrementing at dequeue would open a one-slot window in
   * the submitted bound while the request is merely being issued. */
  r=w->head;if(r){w->head=r->next;if(!w->head)w->tail=0;w->issuing=r;
   /* The command window starts at first service and survives requeues; a
    * head that keeps losing the gate cannot extend it by ping-ponging. */
   if(!r->deadline)r->deadline=now_ns()+CMD_TIMEOUT_NS;r->waited=false;}
  bool stopping=w->stopping,fault=w->fault;int error=w->error;IOLockUnlock(w->guard);
  if(r){
   IOLockLock(w->guard);bool cancelled=r->cancelled;IOLockUnlock(w->guard);
   if(cancelled||stopping){r->cancelled=true;if(!r->result)r->result=I9100_MFC_ABORTED;
    IOLockLock(w->guard);w->queued--;IOLockUnlock(w->guard);finish(w,r);}
   else if(fault){r->result=error?error:I9100_MFC_IO;
    IOLockLock(w->guard);w->queued--;IOLockUnlock(w->guard);finish(w,r);}
   else{
    int e=0;bool lease_full=false;
    if(r->op==I9100_MFC_WORK_FRAME){
     struct i9100_mfc_native_status st;e=i9100_mfc_native_status(w->native,&st);
     lease_full=!e&&st.dpb_count&&st.dpb_count<32U&&
      ((st.display_ready|st.leased)&((1U<<st.dpb_count)-1U))==((1U<<st.dpb_count)-1U);
     if(!e&&!lease_full)e=i9100_mfc_native_frame_ctl(w->native,r->input,r->bytes,r->tag,(int)r->last,w->ctl);
     if(!e){IOLockLock(w->guard);r->baseline=st.frame_completed;IOLockUnlock(w->guard);}
    }else e=i9100_mfc_native_start_ctl(w->native,&r->profile,r->input,r->bytes,r->stream,r->stream_bytes,w->ctl);
    if(lease_full){
     /* All output slots borrowed: requeue and wait for an actual release
      * event wake instead of arming any retry timer. Still counted. */
     IOLockLock(w->guard);r->next=w->head;w->head=r;if(!w->tail)w->tail=r;r->waited=true;w->issuing=0;IOLockUnlock(w->guard);
    }else if(gate_busy(e)){
     /* Engine/board gate held elsewhere: bounded defer, never spin. The
      * terminal check runs before relinking so a timed-out request is never
      * left dangling on the queue head. */
     int d=defer(w,r);
     if(d<0){IOLockLock(w->guard);w->queued--;IOLockUnlock(w->guard);finish(w,r);}
     else{IOLockLock(w->guard);r->next=w->head;w->head=r;if(!w->tail)w->tail=r;r->waited=true;w->issuing=0;IOLockUnlock(w->guard);}
    }else if(e){r->result=e;
     IOLockLock(w->guard);w->queued--;IOLockUnlock(w->guard);finish(w,r);}
    else{
     IOLockLock(w->guard);w->queued--;w->active=r;w->issuing=0;IOLockUnlock(w->guard);
     int ae=line_arm(w);if(gate_busy(ae)){if(defer(w,r)<0)finish(w,r);}
     /* A failed arm must not drop the just-issued hardware command: the
      * client hears the arm error once, and poll-only retirement continues
      * until the engine or a proven shutdown retires it. */
     else if(ae)retire_begin(w,r,ae);
     else w->timer->setTimeoutUS(DEFER_US);
    }
   }
  }
 }
 IOLockLock(w->guard);bool head=w->head!=0,act=w->active!=0,closed=w->closed,waited=head&&w->head->waited;IOLockUnlock(w->guard);
 if(!head&&!act)line_disarm(w);
 /* Only an immediately runnable queue head deserves another pass; an
  * in-flight command waits for a real notify/timer/stop wake, and a head
  * just deferred above must not spin the pump inside this pass either. */
 return !closed&&head&&!act&&!waited;
}
bool I9100MFCEvents::checkForWork()
{
 /* Caller callbacks may drop the final external reference; hold both. */
 retain();i9100_mfc_worker *w=client;w->retain();
 bool more=enabled?process(w):false;
 w->release();release();return more;
}
static int admission(i9100_mfc_worker *w)
{return w->closed||w->stopping?I9100_MFC_DISABLED:w->fault?w->error?w->error:I9100_MFC_IO:0;}
static int enqueue(i9100_mfc_worker *w,mfc_request *r)
{
 IOLockLock(w->guard);int e=admission(w);
 /* The eight-request bound covers every request class the engine still has
  * to account for: queued heads, the issued command, cancelled-but-not-yet-
  * finished requests, and requests whose callback currently owns the gate. */
 if(!e&&w->queued+(w->active!=0)+w->cancel_pending+w->callbacks>=I9100_MFC_WORKER_QUEUE)e=I9100_MFC_BUSY;
 /* No ticking deadline while merely queued: residency behind the head is
  * transitively bounded by each predecessor's own command window. The clock
  * starts at dequeue so issue retries and in-flight service share one bound. */
 if(!e){r->next=0;
  if(w->tail)w->tail->next=r;else w->head=r;w->tail=r;w->queued++;inc(&w->submitted);
  if(w->added)w->event->wake();}
 IOLockUnlock(w->guard);return e;
}
static mfc_request *make_request(void *owner,uint64_t token,unsigned op,const void *data,uint32_t bytes,i9100_mfc_worker_done done)
{
 mfc_request *r=(mfc_request *)IOMalloc(sizeof(*r));if(!r)return 0;memset(r,0,sizeof(*r));
 r->owner=owner;r->token=token;r->op=op;r->bytes=bytes;r->done=done;
 if(bytes){ /* Copy private input at submission; caller may mutate/free. */
  r->input=(uint8_t *)IOMalloc(bytes);if(!r->input){IOFree(r,sizeof(*r));return 0;}
  memcpy(r->input,data,bytes);
 }
 return r;
}
int i9100_mfc_worker_boot(i9100_mfc_worker *w,void *owner,uint64_t token,
 const i9100_mfc_firmware_profile *profile,const void *firmware,uint32_t firmware_bytes,
 const void *stream,uint32_t stream_bytes,i9100_mfc_worker_done done)
{
 if(!w||!w->guard||!owner||!profile||!firmware||!firmware_bytes||firmware_bytes>I9100_MFC_WORKER_INPUT_MAX||
    stream_bytes>I9100_MFC_WORKER_INPUT_MAX||(stream==0)!=(stream_bytes==0U)||!done||!thread_context())return I9100_MFC_ARGUMENT;
 mfc_request *r=make_request(owner,token,I9100_MFC_WORK_BOOT,firmware,firmware_bytes,done);if(!r)return I9100_MFC_IO;
 r->profile=*profile;
 if(stream_bytes){r->stream=(uint8_t *)IOMalloc(stream_bytes);
  if(!r->stream){free_request(r);return I9100_MFC_IO;}memcpy(r->stream,stream,stream_bytes);r->stream_bytes=stream_bytes;}
 int e=enqueue(w,r);if(e)free_request(r);return e;
}
int i9100_mfc_worker_frame(i9100_mfc_worker *w,void *owner,uint64_t token,
 const void *data,uint32_t bytes,uint32_t tag,int last,i9100_mfc_worker_done done)
{
 if(!w||!w->guard||!owner||bytes>I9100_MFC_WORKER_INPUT_MAX||(!data&&bytes)||(last!=0&&last!=1)||!done||!thread_context())
  return I9100_MFC_ARGUMENT;
 mfc_request *r=make_request(owner,token,I9100_MFC_WORK_FRAME,data,bytes,done);if(!r)return I9100_MFC_IO;
 r->tag=tag;r->last=(unsigned)last;int e=enqueue(w,r);if(e)free_request(r);return e;
}
void i9100_mfc_worker_cancel(i9100_mfc_worker *w,void *owner,uint64_t token)
{
 if(!w||!w->guard||!owner||!thread_context())return;
 /* Unlinked requests park on the cancelled list; the workloop thread
  * finishes them under its own gate. The caller's context never opens or
  * closes a gate it does not own, and the dequeue->active publication
  * window is covered through w->issuing. */
 mfc_request **link,*tail=0;
 IOLockLock(w->guard);
 for(link=&w->head;*link;){
  mfc_request *r=*link;
  if(r->owner==owner&&r->token==token){*link=r->next;w->queued--;r->cancelled=true;
   r->next=w->cancelled_head;w->cancelled_head=r;w->cancel_pending++;}
  else{link=&r->next;tail=r;}
 }
 w->tail=tail;
 /* A queued-or-issuing request is unlinked and finishes ABORTED on the
  * workloop. A request already issued to the engine is only marked: it
  * stays active and is retired by the real IRQ/poll/deadline path, with the
  * armed line and its timer kept as its retirement services. */
 if(w->issuing&&w->issuing->owner==owner&&w->issuing->token==token)w->issuing->cancelled=true;
 if(w->active&&w->active->owner==owner&&w->active->token==token)w->active->cancelled=true;
 if(w->added)w->event->wake();IOLockUnlock(w->guard);
}
int i9100_mfc_worker_frame_lease(i9100_mfc_worker *w,struct i9100_mfc_frame_lease *lease)
{
 if(!w||!w->guard||!lease||!thread_context())return I9100_MFC_ARGUMENT;
 IOLockLock(w->guard);int e=admission(w);IOLockUnlock(w->guard);
 if(e)return e;
 return i9100_mfc_native_acquire_ctl(w->native,lease,w->ctl);
}
int i9100_mfc_worker_frame_return(i9100_mfc_worker *w,struct i9100_mfc_frame_lease *lease)
{
 if(!w||!w->guard||!lease||!thread_context())return I9100_MFC_ARGUMENT;
 int e=i9100_mfc_native_release(w->native,lease);
 if(!e)wake(w); /* release event unblocks backpressured queue */
 return e;
}
static bool pending(i9100_mfc_worker *w){return w->queued||w->active||w->issuing||w->callbacks||w->cancel_pending;}
/* The drain bound covers only requests that can still retire by real
 * service. A timed-out retirement command cannot outlive the shutdown reset
 * below — waiting out the firmware's own deadline clock is exactly the
 * abandonment the retirement owner exists to prevent. */
static bool drainable(i9100_mfc_worker *w)
{mfc_request *a=w->active;return w->queued||w->issuing||w->callbacks||w->cancel_pending||(a&&!a->timed_out);}
int i9100_mfc_worker_stop(i9100_mfc_worker *w)
{
 if(!w||!w->guard||!thread_context())return I9100_MFC_ARGUMENT;
 IOLockLock(w->guard);
 if(w->loop->workerOnThread()){bool busy=pending(w);IOLockUnlock(w->guard);return busy?I9100_MFC_BUSY:0;}
 /* Preflight before any state migration: a borrowed output frame must be
  * returned first, so refuse with the engine untouched rather than abort
  * queued work and only then discover the block. */
 struct i9100_mfc_native_status pre;int pe=i9100_mfc_native_status(w->native,&pre);
 if(!pe&&pre.leased){IOLockUnlock(w->guard);return I9100_MFC_BUSY;}
 if(pe&&!gate_busy(pe)){IOLockUnlock(w->guard);return pe;}
 /* Stopping stays latched once set: the drain below may already cancel
  * queued requests and migrate engine state, so a later transient failure
  * reports BUSY while the flag remains true — never rolled back into a
  * lying "live engine". */
 w->stopping=true;if(w->added)w->event->wake();
 /* Bounded wait for requests that can still retire by real service; a
  * timed-out retirement command is retired by the proven shutdown below. */
 for(unsigned spins=0;drainable(w);spins++){
  if(spins>=60000U){IOLockUnlock(w->guard);return I9100_MFC_TIMEOUT;}
  IOLockUnlock(w->guard);IOSleep(1);IOLockLock(w->guard);
 }
 IOLockUnlock(w->guard);
 /* Real engine shutdown on every pass: the bound control lease proves
  * identity, i9100_mfc_stop performs the actual reset/MC-wait/mailbox
  * quiesce, and an independent readback verifies RISC/MC/mailboxes/latch
  * plus both MMU domains. A faulted registry slot is repaired by the exact
  * owner (mask + proven dispatch/EOI quiet) so teardown can proceed; any
  * failure keeps the lease, arenas and IRQ binding for a same-stage retry. */
 for(unsigned n=0;n<1000U;n++){
  int de=line_disarm(w);
  if(de==I9100_MP_OWNER&&w->irq){
   int re=i9100_mfc_irq_repair(w->irq);
   if(re==I9100_MP_BUSY){IOSleep(1);continue;}
   if(re){IOLockLock(w->guard);if(!w->error)w->error=re;IOLockUnlock(w->guard);return re;}
   de=line_disarm(w);
  }
  if(de&&!gate_busy(de))return de;
  int se=w->ctl_bound?i9100_mfc_native_shutdown_ctl(w->native,w->ctl):0;
  if(se&&!gate_busy(se))return se;
  struct i9100_mfc_native_status st;int e=i9100_mfc_native_status(w->native,&st);
  if(!e&&(st.pending||st.leased))e=I9100_MFC_BUSY;
  if(!e&&w->irq){
   struct i9100_mfc_irq_status qs;e=i9100_mfc_irq_status(w->irq,&qs);
   if(e==I9100_MP_OWNER){int re=i9100_mfc_irq_repair(w->irq);e=re?re:I9100_MFC_BUSY;}
   else if(!e&&(qs.enabled||qs.active||qs.dispatch))e=I9100_MFC_BUSY;
  }
  if(!e&&!se){
   /* Hardware proven quiet: the timed-out retirement bookkeeping the drain
    * skipped now retires on the workloop against the proven-quiet mailbox;
    * stop only completes once that accounting is gone too. */
   for(unsigned spins=0;;spins++){
    if(spins>=60000U)return I9100_MFC_TIMEOUT;
    wake(w);IOSleep(1);
    IOLockLock(w->guard);bool left=pending(w);IOLockUnlock(w->guard);
    if(!left)return 0;
   }
  }
  if(!e)e=se;
  if(!gate_busy(e))return e;
  IOSleep(1);
 }
 return I9100_MFC_BUSY; /* could not prove quiescence inside the bound */
}
int i9100_mfc_worker_destroy(i9100_mfc_worker **owner)
{
 if(!owner||!*owner||!thread_context())return I9100_MFC_ARGUMENT;i9100_mfc_worker *w=*owner;
 /* A callback-thread destroy would tear down the gate it runs under:
  * refuse cleanly so the caller can destroy from its own context later. */
 if(w->loop&&w->loop->workerOnThread())return I9100_MFC_BUSY;
 /* A retained worker retries the same teardown stages; each is idempotent
  * and re-verified against actual quiescence, so a failed cleanup preserves
  * the original error and owner yet stays explicitly recoverable. */
 int e=w->added?i9100_mfc_worker_stop(w):0;
 if(!e&&w->added){IOLockLock(w->guard);w->added=false;IOLockUnlock(w->guard);
  e=w->loop->removeEventSource(w->event);if(!e)w->added=false;
  else{IOLockLock(w->guard);w->added=true;IOLockUnlock(w->guard);}}
 if(!e&&w->timer_added){e=w->loop->removeEventSource(w->timer);if(!e)w->timer_added=false;}
 /* The IRQ lease must close before the native control lease is dropped:
  * while a faulted slot could refuse teardown, the bound control still
  * proves this worker is the owner that must finish masking/releasing —
  * dropping it first would leave raw callers able to seize the engine. */
 if(!e&&w->irq){int qe=i9100_mfc_irq_close(&w->irq);if(qe)e=qe;else{IOFree(w->irq_storage,i9100_mfc_irq_storage());w->irq_storage=0;}}
 if(!e&&w->ctl_bound){e=i9100_mfc_native_control_drop(w->native,w->ctl);if(!e)w->ctl_bound=false;}
 if(!e&&w->held){int qe=i9100_mfc_native_hold_put(w->native,(uintptr_t)w);if(!qe)w->held=false;else e=qe;}
 if(e){preserve(w,e);return e;}
 unpreserve(w);
 if(w->guard)IOLockLock(w->guard);w->closed=true;w->native=0;w->board=0;
 if(w->guard)IOLockUnlock(w->guard);*owner=0;w->release();return 0;
}
void i9100_mfc_worker::free()
{if(event)event->release();if(timer)timer->release();if(loop)loop->release();if(guard)IOLockFree(guard);OSObject::free();}
int i9100_mfc_worker_create(int enabled,i9100_mfc_native *native,i9100_mfc_board *board,i9100_mfc_worker **out)
{
 if(!out||*out||enabled!=1||!native||!board||!thread_context())return I9100_MFC_ARGUMENT;
 i9100_mfc_worker *w=new i9100_mfc_worker;if(!w)return I9100_MFC_IO;
 if(!w->initialize()){w->release();return I9100_MFC_IO;}w->native=native;w->board=board;*out=w;
 w->guard=IOLockAlloc();w->loop=new I9100MFCLoop;w->event=new I9100MFCEvents;w->timer=IOTimerEventSource::timerEventSource((OSObject *)w,on_timeout);
 int e=!w->guard||!w->loop||!w->event||!w->timer?I9100_MFC_IO:0;
 if(!e&&!w->loop->configure())e=I9100_MFC_IO;
 if(!e&&!w->event->configure(w))e=I9100_MFC_IO;
 if(!e)e=i9100_mfc_native_hold_get(native,(uintptr_t)w);if(!e)w->held=true;
 if(!e){w->irq_storage=IOMalloc(i9100_mfc_irq_storage());if(!w->irq_storage)e=I9100_MFC_IO;}
 if(!e){int oe=i9100_mfc_irq_open(1,board,w->irq_storage,i9100_mfc_irq_storage(),&w->irq);
  if(oe){IOFree(w->irq_storage,i9100_mfc_irq_storage());w->irq_storage=0;e=oe;}}
 if(!e){i9100_mfc_irq_notify sink={(void *)w,irq_signal};e=i9100_mfc_irq_bind(w->irq,&sink);}
 /* The exclusive controller lease pairs this worker's board-issued IRQ
  * token with the native context and live supply generation; raw command
  * entry points are refused from here on. */
 if(!e){w->ctl=i9100_mfc_irq_token_id(w->irq);
  e=i9100_mfc_native_control_bind(native,w->ctl);if(!e)w->ctl_bound=true;}
 if(!e){e=w->loop->addEventSource(w->event);if(!e)w->added=true;}
 if(!e){e=w->loop->addEventSource(w->timer);if(!e)w->timer_added=true;}
 if(e){int cleanup=i9100_mfc_worker_destroy(out);return cleanup?cleanup:e;}return 0;
}
int i9100_mfc_worker_snapshot(i9100_mfc_worker *w,struct i9100_mfc_worker_status *out)
{
 if(!w||!w->guard||!out||!thread_context())return I9100_MFC_ARGUMENT;IOLockLock(w->guard);
 struct i9100_mfc_worker_status s={w->submitted,w->completed,w->cancelled,w->interrupts,w->defers,w->late_irq,w->timeouts,
  w->queued,(unsigned)(w->active!=0),w->armed,w->callbacks,w->stopping,w->fault,w->closed,w->held,(unsigned)(w->irq!=0),
  w->ctl_bound,w->cancel_pending,w->error,w->cleanup_error};
 *out=s;IOLockUnlock(w->guard);return 0;
}
