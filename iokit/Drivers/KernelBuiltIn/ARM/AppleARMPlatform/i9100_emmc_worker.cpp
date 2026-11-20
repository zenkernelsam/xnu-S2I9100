/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_worker.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <machine/machine_routines.h>
#include <kern/clock.h>
#include <string.h>
enum {WORK_CAPACITY=16,WORK_MAX_BYTES=1024*1024,WORK_CHUNK=512*I9100_EMMC_MAX_SECTORS};
struct emmc_request {
 emmc_request *next;OSObject *owner;UInt64 token,lba,bytes,actual;
 IOMemoryDescriptor *buffer;IOStorageCompletion completion;i9100_emmc_work_validate validate;
 unsigned operation;bool cancelled,prepared,waiting,finished,abandoned;IOReturn result;
};
class I9100EMMCLoop : public IOWorkLoop {
 OSDeclareDefaultStructors(I9100EMMCLoop)
public:
 bool configure(){return IOWorkLoop::init();}
 void callbackGateOpen(){IOWorkLoop::openGate();}
 void callbackGateClose(){IOWorkLoop::closeGate();}
};
class I9100EMMCEvents : public IOEventSource {
 OSDeclareDefaultStructors(I9100EMMCEvents)
 i9100_emmc_worker *client;
public:
 bool configure(i9100_emmc_worker *);
 void wake(){signalWorkAvailable();}
 bool checkForWork() override;
};
struct i9100_emmc_worker : public OSObject {
 OSDeclareDefaultStructors(i9100_emmc_worker)
public:
 i9100_emmc_native *native;I9100EMMCLoop *loop;I9100EMMCEvents *event;IOLock *guard;
 emmc_request *head,*tail,*active,*failed_memory;uint8_t *bounce;i9100_emmc_worker *retained_next;
 bool added,held,stopping,fault,closed,retained;unsigned queued,callbacks,quarantined;
 uint64_t submitted,completed,cancelled,read_bytes,written_bytes;int error,cleanup_error;
 bool initialize(){native=0;loop=0;event=0;guard=0;head=tail=active=failed_memory=0;bounce=0;retained_next=0;
  added=held=stopping=fault=closed=retained=false;queued=callbacks=quarantined=0;
  submitted=completed=cancelled=read_bytes=written_bytes=0;error=cleanup_error=0;return OSObject::init();}
 void free() override;
};
OSDefineMetaClassAndStructors(I9100EMMCEvents,IOEventSource)
OSDefineMetaClassAndStructors(I9100EMMCLoop,IOWorkLoop)
OSDefineMetaClassAndStructors(i9100_emmc_worker,OSObject)
static i9100_emmc_worker *volatile retained_failed;
static void preserve(i9100_emmc_worker *w,int e)
{
 w->cleanup_error=e;if(w->retained)return;w->retained=true;w->retain();
 do{w->retained_next=retained_failed;}while(!__sync_bool_compare_and_swap(&retained_failed,w->retained_next,w));
}
static IOReturn io_error(int e)
{return !e?kIOReturnSuccess:e==I9100_EMMC_BUSY?kIOReturnBusy:e==I9100_EMMC_ARGUMENT?kIOReturnBadArgument:
 e==I9100_EMMC_PARTITION?kIOReturnNotWritable:e==I9100_EMMC_NO_DEVICE?kIOReturnNoMedia:
 e==I9100_EMMC_UNSUPPORTED?kIOReturnUnsupported:e==I9100_EMMC_TIMEOUT?kIOReturnTimeout:kIOReturnIOError;}
static bool context(){return ml_get_interrupts_enabled()&&!ml_at_interrupt_context();}
static void inc(uint64_t *value){if(*value!=~(uint64_t)0)(*value)++;}
static IOReturn admission(i9100_emmc_worker *w)
{return w->closed||w->stopping?kIOReturnNotOpen:w->fault?io_error(w->error):kIOReturnSuccess;}
static bool pending(i9100_emmc_worker *w){return w->head||w->active||w->callbacks;}
static void notify(i9100_emmc_worker *w){IOLockWakeup(w->guard,w,false);}
static void wake_locked(i9100_emmc_worker *w){if(w->added)w->event->wake();}
bool I9100EMMCEvents::configure(i9100_emmc_worker *w)
{client=w;return IOEventSource::init(w,0);}
static void release_request(emmc_request *r)
{if(r->buffer)r->buffer->release();if(r->owner)r->owner->release();IOFree(r,sizeof(*r));}
static void finish(i9100_emmc_worker *w,emmc_request *r)
{
 bool keep_memory=false;
 if(r->prepared){IOReturn e=r->buffer->complete(r->operation==I9100_EMMC_WORK_WRITE?kIODirectionOut:kIODirectionIn);
  if(e){if(!r->result)r->result=e;keep_memory=true;IOLockLock(w->guard);w->fault=true;
   if(!w->error)w->error=e;r->next=w->failed_memory;w->failed_memory=r;w->quarantined++;preserve(w,e);IOLockUnlock(w->guard);
  }else r->prepared=false;}
 IOLockLock(w->guard);w->active=0;inc(&w->completed);if(r->cancelled)inc(&w->cancelled);
 if(r->waiting&&!r->abandoned){r->finished=true;notify(w);IOLockUnlock(w->guard);return;}
 w->callbacks++;IOLockUnlock(w->guard);
 /* IOWorkLoop dispatch holds its recursive gate. Drop it around arbitrary
  * caller callbacks and owner release, so close/reentrant IO can enqueue. */
 w->loop->callbackGateOpen();
 if(!r->waiting)IOStorage::complete(&r->completion,r->result,r->actual);
 IOLockLock(w->guard);w->callbacks--;notify(w);IOLockUnlock(w->guard);
 /* Source may have been removed during owner release; the retained worker
  * still owns the actual loop and can restore its gate before returning. */
 if(!keep_memory)release_request(r);w->loop->callbackGateClose();
}
static bool process(i9100_emmc_worker *w)
{
 IOLockLock(w->guard);
 emmc_request *r=w->active;
 if(!r&&w->head){r=w->head;w->head=r->next;if(!w->head)w->tail=0;w->queued--;w->active=r;}
 if(!r){IOLockUnlock(w->guard);return false;}
 if(w->stopping)r->cancelled=true;bool cancel=r->cancelled;bool fault=w->fault;int error=w->error;
 IOLockUnlock(w->guard);
 if(cancel)r->result=kIOReturnAborted;else if(fault)r->result=io_error(error);
 else r->result=r->validate(r->owner,r->token,r->operation);
 if(!r->result&&!r->prepared&&r->buffer){
  r->result=r->buffer->prepare(r->operation==I9100_EMMC_WORK_WRITE?kIODirectionOut:kIODirectionIn);
  if(!r->result)r->prepared=true;
 }
 IOLockLock(w->guard);if(r->cancelled||w->stopping){r->cancelled=true;if(!r->result)r->result=kIOReturnAborted;}IOLockUnlock(w->guard);
 if(!r->result){
  UInt64 n=r->bytes-r->actual;if(n>WORK_CHUNK)n=WORK_CHUNK;int e=0;
  if(r->operation==I9100_EMMC_WORK_SYNC)e=i9100_emmc_native_synchronize(w->native);
  else if(r->operation==I9100_EMMC_WORK_SEAL)e=i9100_emmc_native_seal_data(w->native);
  else if(r->operation==I9100_EMMC_WORK_WRITE){
   if(r->buffer->readBytes(r->actual,w->bounce,n)!=n)r->result=kIOReturnUnderrun;
   else e=i9100_emmc_native_write_data(w->native,r->lba+r->actual/512U,(uint32_t)(n/512U),w->bounce);
  }else e=i9100_emmc_native_read(w->native,r->lba+r->actual/512U,(uint32_t)(n/512U),w->bounce);
  if(e){r->result=io_error(e);IOLockLock(w->guard);w->fault=true;if(!w->error)w->error=e;IOLockUnlock(w->guard);}
  if(!r->result&&r->buffer){
   if(r->operation==I9100_EMMC_WORK_READ){UInt64 copied=r->buffer->writeBytes(r->actual,w->bounce,n);
    if(copied>n)copied=n;r->actual+=copied;if(copied!=n)r->result=kIOReturnUnderrun;
    IOLockLock(w->guard);w->read_bytes=~(uint64_t)0-w->read_bytes<copied?~(uint64_t)0:w->read_bytes+copied;IOLockUnlock(w->guard);
   }else{r->actual+=n;IOLockLock(w->guard);w->written_bytes=~(uint64_t)0-w->written_bytes<n?~(uint64_t)0:w->written_bytes+n;IOLockUnlock(w->guard);}
  }
 }
 IOLockLock(w->guard);cancel=r->cancelled||w->stopping;if(cancel){r->cancelled=true;if(!r->result)r->result=kIOReturnAborted;}
 bool done=r->result||!r->buffer||r->actual==r->bytes;IOLockUnlock(w->guard);
 if(done)finish(w,r);
 IOLockLock(w->guard);bool more=!w->closed&&(w->head||w->active);IOLockUnlock(w->guard);return more;
}
bool I9100EMMCEvents::checkForWork()
{
 /* A last caller may release its service in its completion callback. Hold
  * both event and worker until this dispatch returns, including teardown. */
 retain();i9100_emmc_worker *w=client;w->retain();bool more=enabled?process(w):false;
 w->release();release();return more;
}
static IOReturn enqueue(i9100_emmc_worker *w,emmc_request *r)
{
 IOLockLock(w->guard);IOReturn e=admission(w);
 if(!e&&w->queued+(w->active!=0)>=WORK_CAPACITY)e=kIOReturnBusy;
 if(!e){r->owner->retain();if(r->buffer)r->buffer->retain();r->next=0;
  if(w->tail)w->tail->next=r;else w->head=r;w->tail=r;w->queued++;inc(&w->submitted);wake_locked(w);}
 IOLockUnlock(w->guard);return e;
}
IOReturn i9100_emmc_worker_submit(i9100_emmc_worker *w,OSObject *owner,UInt64 token,i9100_emmc_work_validate validate,
 unsigned operation,UInt64 lba,UInt64 sectors,IOMemoryDescriptor *buffer,const IOStorageCompletion *completion)
{
 if(!w||!w->guard||!owner||!validate||!buffer||!completion||!completion->action||!context())return kIOReturnBadArgument;
 if((operation!=I9100_EMMC_WORK_READ&&operation!=I9100_EMMC_WORK_WRITE)||!sectors||sectors>WORK_MAX_BYTES/512U||
    lba>=30777344ULL||sectors>30777344ULL-lba||buffer->getLength()!=sectors*512U||
    !(buffer->getDirection()&(operation==I9100_EMMC_WORK_WRITE?kIODirectionOut:kIODirectionIn)))return kIOReturnBadArgument;
 emmc_request *r=(emmc_request *)IOMalloc(sizeof(*r));if(!r)return kIOReturnNoMemory;memset(r,0,sizeof(*r));
 r->owner=owner;r->token=token;r->validate=validate;r->lba=lba;r->bytes=sectors*512U;r->buffer=buffer;r->completion=*completion;r->operation=operation;
 IOReturn e=enqueue(w,r);if(e)IOFree(r,sizeof(*r));return e;
}
IOReturn i9100_emmc_worker_barrier(i9100_emmc_worker *w,OSObject *owner,UInt64 token,i9100_emmc_work_validate validate,unsigned operation)
{
 if(!w||!w->guard||!owner||!validate||!context()||(operation!=I9100_EMMC_WORK_SYNC&&operation!=I9100_EMMC_WORK_SEAL))return kIOReturnBadArgument;
 if(w->loop->onThread())return kIOReturnBusy;
 emmc_request *r=(emmc_request *)IOMalloc(sizeof(*r));if(!r)return kIOReturnNoMemory;memset(r,0,sizeof(*r));
 r->owner=owner;r->token=token;r->validate=validate;r->operation=operation;r->waiting=true;
 w->retain();IOReturn e=enqueue(w,r);if(e){IOFree(r,sizeof(*r));w->release();return e;}
 AbsoluteTime deadline;clock_interval_to_deadline(60,kSecondScale,&deadline);IOLockLock(w->guard);
 while(!r->finished){int wait=IOLockSleepDeadline(w->guard,w,deadline,THREAD_UNINT);
  if(wait==THREAD_TIMED_OUT&&!r->finished){r->abandoned=r->cancelled=true;w->stopping=true;e=kIOReturnTimeout;break;}}
 if(r->finished)e=r->result;bool complete=r->finished;if(!complete)wake_locked(w);IOLockUnlock(w->guard);
 if(complete)release_request(r);w->release();return e;
}
void i9100_emmc_worker_cancel(i9100_emmc_worker *w,OSObject *owner,UInt64 token)
{
 if(!w||!w->guard||!owner||!context())return;IOLockLock(w->guard);
 for(emmc_request *r=w->head;r;r=r->next)if(r->owner==owner&&r->token==token)r->cancelled=true;
 if(w->active&&w->active->owner==owner&&w->active->token==token)w->active->cancelled=true;
 wake_locked(w);IOLockUnlock(w->guard);
}
IOReturn i9100_emmc_worker_stop(i9100_emmc_worker *w)
{
 if(!w||!w->guard||!context())return kIOReturnBadArgument;
 IOLockLock(w->guard);w->stopping=true;wake_locked(w);IOLockUnlock(w->guard);
 IOLockLock(w->guard);
 if(w->loop->onThread()){bool busy=pending(w);IOLockUnlock(w->guard);return busy?kIOReturnBusy:kIOReturnSuccess;}
 AbsoluteTime deadline;clock_interval_to_deadline(60,kSecondScale,&deadline);
 while(pending(w)){int wait=IOLockSleepDeadline(w->guard,w,deadline,THREAD_UNINT);
  if(wait==THREAD_TIMED_OUT&&pending(w)){IOLockUnlock(w->guard);return kIOReturnTimeout;}}
 IOLockUnlock(w->guard);return kIOReturnSuccess;
}
int i9100_emmc_worker_destroy(i9100_emmc_worker **owner)
{
 if(!owner||!*owner||!context())return I9100_EMMC_ARGUMENT;i9100_emmc_worker *w=*owner;
 if(w->retained)return w->cleanup_error?w->cleanup_error:I9100_EMMC_BUSY;
 IOReturn e=w->added?i9100_emmc_worker_stop(w):kIOReturnSuccess;
 if(!e&&w->added){IOLockLock(w->guard);w->added=false;IOLockUnlock(w->guard);
  e=w->loop->removeEventSource(w->event);if(e){IOLockLock(w->guard);w->added=true;IOLockUnlock(w->guard);}}
 if(!e&&w->held){e=i9100_emmc_native_hold_put(w->native,(uintptr_t)w);if(!e)w->held=false;}
 if(e){preserve(w,e);return e;}if(w->guard)IOLockLock(w->guard);w->closed=true;w->native=0;
 if(w->guard)IOLockUnlock(w->guard);*owner=0;w->release();return 0;
}
void i9100_emmc_worker::free()
{if(event)event->release();if(loop)loop->release();if(bounce)IOFree(bounce,WORK_CHUNK);if(guard)IOLockFree(guard);OSObject::free();}
int i9100_emmc_worker_create(int enabled,i9100_emmc_native *native,i9100_emmc_worker **out)
{
 if(!out||*out||(enabled!=0&&enabled!=1))return I9100_EMMC_ARGUMENT;if(!enabled)return I9100_EMMC_UNSUPPORTED;
 if(!native||!context())return I9100_EMMC_ARGUMENT;
 int state=i9100_emmc_native_validate(native);if(state)return state;
 i9100_emmc_worker *w=new i9100_emmc_worker;if(!w)return I9100_EMMC_IO;
 if(!w->initialize()){w->release();return I9100_EMMC_IO;}w->native=native;*out=w;
 w->guard=IOLockAlloc();w->bounce=(uint8_t *)IOMalloc(WORK_CHUNK);w->loop=new I9100EMMCLoop;w->event=new I9100EMMCEvents;
 int e=!w->guard||!w->bounce||!w->loop||!w->event?I9100_EMMC_IO:0;
 if(!e&&!w->loop->configure())e=I9100_EMMC_IO;
 if(!e&&!w->event->configure(w))e=I9100_EMMC_IO;
 if(!e){e=i9100_emmc_native_hold_get(native,(uintptr_t)w);if(!e)w->held=true;}
 if(!e){e=w->loop->addEventSource(w->event);if(!e)w->added=true;}
 if(e){int cleanup=i9100_emmc_worker_destroy(out);return cleanup?cleanup:e;}return 0;
}
int i9100_emmc_worker_snapshot(i9100_emmc_worker *w,i9100_emmc_worker_status *out)
{
 if(!w||!w->guard||!out||!context())return I9100_EMMC_ARGUMENT;IOLockLock(w->guard);
 i9100_emmc_worker_status s={w->submitted,w->completed,w->cancelled,w->read_bytes,w->written_bytes,
  w->queued,(unsigned)(w->active!=0),w->callbacks,w->stopping,w->fault,w->closed,w->held,w->quarantined,w->error,w->cleanup_error};
 *out=s;IOLockUnlock(w->guard);return 0;
}
