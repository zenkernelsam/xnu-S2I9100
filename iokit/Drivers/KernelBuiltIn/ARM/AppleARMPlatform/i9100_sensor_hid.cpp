/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sensor_hid.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef nanoseconds_to_absolutetime
extern "C" void nanoseconds_to_absolutetime(uint64_t,uint64_t *);
enum {SH_START=1,SH_STOP,SH_RECOVER,SH_READ,SH_STATUS,SH_DETACH,SH_GET,SH_PUT,SH_BIND,SH_HOLD_GET,SH_HOLD_PUT,SH_CAPACITY=16};
struct sensor_hid_state {
 IOWorkLoop *loop;IOTimerEventSource *timer;i9100_sensor_worker *worker;
 i9100_sensor_consumer_lease lease;i9100_sensor_hid_profile profile;
 i9100_sensor_hid_record queue[3][SH_CAPACITY];uint32_t head[3],count[3],added;
 struct i9100_sensor_hid_status status;
 i9100_sensor_hid_consumer *consumers[3];uint32_t generations[3];
 i9100_sensor_hid_hold *service_hold;uint32_t service_generation;
};
struct i9100_sensor_hid : public OSObject {
 OSDeclareDefaultStructors(i9100_sensor_hid)
public:
 sensor_hid_state s;
 bool initialize(){memset(&s,0,sizeof(s));return OSObject::init();}
 virtual void free();
};
OSDefineMetaClassAndStructors(i9100_sensor_hid,OSObject)
static void inc(uint64_t *n){if(*n!=~(uint64_t)0)(*n)++;}
static bool outside(const void *object,size_t bytes,const void *p,size_t length)
{uintptr_t a=(uintptr_t)object,b=(uintptr_t)p;return object&&p&&a<=~(uintptr_t)0-bytes&&b<=~(uintptr_t)0-length&&!(a+bytes>b&&b+length>a);}
static int native_ticks(void *,uint64_t ns,uint64_t *out)
{if(!ns||!out)return I9100_SH_CLOCK;nanoseconds_to_absolutetime(ns,out);return *out?0:I9100_SH_CLOCK;}
void i9100_sensor_hid_native_clock(struct i9100_sensor_hid_clock *out)
{if(out){out->cookie=0;out->absolute_ticks=native_ticks;}}
static void sources_off(i9100_sensor_hid *p)
{p->s.status.running=0;if(p->s.timer){p->s.timer->disable();p->s.timer->cancelTimeout();}}
static int halt(i9100_sensor_hid *p,int recover)
{
 sources_off(p);p->s.status.closing=1;
 if(p->s.lease.owned){int e=i9100_sensor_worker_consumer_stop(&p->s.lease,recover);
  if(e){p->s.status.cleanup_error=e;p->s.status.fault=1;if(!p->s.status.error)p->s.status.error=e;return e;}
  e=i9100_sensor_worker_consumer_put(&p->s.lease);if(e){p->s.status.cleanup_error=e;p->s.status.fault=1;if(!p->s.status.error)p->s.status.error=e;return e;}}
 p->s.status.ready=0;p->s.status.closed=1;p->s.status.worker_owned=0;p->s.status.cleanup_error=0;return 0;
}
static void fault(i9100_sensor_hid *p,int e)
{p->s.status.fault=1;if(!p->s.status.error)p->s.status.error=e;(void)halt(p,0);}
static void discard(i9100_sensor_hid *p,unsigned n)
{for(unsigned i=0;i<n;i++)inc(&p->s.status.discarded);}
static void process(i9100_sensor_hid *p)
{
 if(!p->s.status.running||p->s.status.fault||p->s.status.closing)return;
 inc(&p->s.status.timer_ticks);
 struct i9100_sensor_worker_status state={};int e=i9100_sensor_worker_status(p->s.worker,&state);
 if(e){fault(p,e);return;}
 if(state.fault||!state.ready||!state.running||state.mask!=p->s.status.mask||!state.consumer_owned){fault(p,state.error?state.error:I9100_SH_OWNER);return;}
 if(state.consumer_generation!=p->s.lease.generation){fault(p,I9100_SH_OWNER);return;}
 i9100_sensor_sample samples[32];uint32_t received=0;
 e=i9100_sensor_worker_consumer_read(&p->s.lease,samples,32,&received);if(e){fault(p,e);return;}
 for(unsigned n=0;n<received;n++){
  const auto &sample=samples[n];unsigned slot=sample.kind-1U;
  if(slot>=3U||!(p->s.status.mask&(1U<<slot))||sample.sequence<=p->s.status.last_sequence[slot]||
     sample.timestamp_ns<=p->s.status.last_ns[slot]){discard(p,received-n);fault(p,I9100_SH_OWNER);return;}
  i9100_sensor_hid_record r={};r.kind=sample.kind;r.sequence=sample.sequence;r.sample_ns=sample.timestamp_ns;
  e=p->s.profile.clock.absolute_ticks(p->s.profile.clock.cookie,sample.timestamp_ns,&r.absolute_ticks);
  if(e||!r.absolute_ticks||r.absolute_ticks<=p->s.status.last_ticks[slot]){discard(p,received-n);fault(p,I9100_SH_CLOCK);return;}
  e=i9100_sensor_hid_encode(&sample,r.absolute_ticks,p->s.profile.sender[slot],&r.packet);
  if(e){discard(p,received-n);fault(p,e);return;}
  if(p->s.count[slot]==SH_CAPACITY){p->s.head[slot]=(p->s.head[slot]+1U)%SH_CAPACITY;p->s.count[slot]--;inc(&p->s.status.dropped);}
  p->s.queue[slot][(p->s.head[slot]+p->s.count[slot])%SH_CAPACITY]=r;p->s.count[slot]++;
  p->s.status.last_sequence[slot]=sample.sequence;p->s.status.last_ns[slot]=sample.timestamp_ns;
  p->s.status.last_ticks[slot]=r.absolute_ticks;inc(&p->s.status.produced);
 }
 if(p->s.timer->setTimeoutUS(p->s.profile.poll_us)!=kIOReturnSuccess)fault(p,I9100_SH_FAULT);
}
static void timer_action(OSObject *owner,IOTimerEventSource *){process((i9100_sensor_hid *)owner);}
struct hid_read_request {uint32_t kind,capacity,*received;i9100_sensor_hid_record *records;};
struct hid_bind_request {uint64_t sender;};
struct hid_get_request {uint32_t kind;};
static bool consumer_valid(i9100_sensor_hid *p,i9100_sensor_hid_consumer *l)
{return l&&l->owned&&l->self==(uintptr_t)l&&l->collector==p&&l->kind>=1U&&l->kind<=3U&&
 p->s.consumers[l->kind-1U]==l&&l->generation==p->s.generations[l->kind-1U];}
static bool separate_leases(i9100_sensor_hid *p,const void *out,size_t bytes)
{
 if(p->s.service_hold&&!outside(p->s.service_hold,sizeof(*p->s.service_hold),out,bytes))return false;
 for(unsigned n=0;n<3;n++)if(p->s.consumers[n]&&!outside(p->s.consumers[n],sizeof(*p->s.consumers[n]),out,bytes))return false;
 return true;
}
static IOReturn control(OSObject *owner,void *operation,void *argument,void *consumer,void *)
{
 auto *p=(i9100_sensor_hid *)owner;
 auto *l=(i9100_sensor_hid_consumer *)consumer;
 switch((uintptr_t)operation){
 case SH_START:{
  if(!p->s.status.ready||p->s.status.running||p->s.status.fault||p->s.status.closing||p->s.status.closed)return I9100_SH_BUSY;
  int e=i9100_sensor_worker_consumer_start(&p->s.lease);if(e){fault(p,e);return e;}
  p->s.status.running=1;p->s.timer->enable();
  e=p->s.timer->setTimeoutUS(p->s.profile.poll_us);if(e){fault(p,I9100_SH_FAULT);return I9100_SH_FAULT;}return 0;}
 case SH_STOP:return halt(p,0);
 case SH_RECOVER:
  if(!p->s.status.fault&&!p->s.status.closing)return I9100_SH_ARGUMENT;return halt(p,1);
 case SH_READ:{
  auto *r=(hid_read_request *)argument;unsigned slot=r->kind-1U;
  if(!separate_leases(p,r->records,r->capacity*sizeof(*r->records))||!separate_leases(p,r->received,sizeof(*r->received)))return I9100_SH_ARGUMENT;
  if(l){if(!consumer_valid(p,l)||l->kind!=r->kind)return I9100_SH_OWNER;}
  else if(p->s.consumers[slot])return I9100_SH_BUSY;
  uint32_t count=p->s.count[slot]<r->capacity?p->s.count[slot]:r->capacity;
  for(unsigned n=0;n<count;n++)r->records[n]=p->s.queue[slot][(p->s.head[slot]+n)%SH_CAPACITY];
  p->s.head[slot]=(p->s.head[slot]+count)%SH_CAPACITY;p->s.count[slot]-=count;
  for(unsigned n=0;n<count;n++)inc(&p->s.status.consumed);*r->received=count;return 0;}
 case SH_STATUS:{if(!separate_leases(p,argument,sizeof(struct i9100_sensor_hid_status)))return I9100_SH_ARGUMENT;
  auto value=p->s.status;for(unsigned n=0;n<3;n++)value.queued[n]=p->s.count[n];
  value.consumer_mask=0;for(unsigned n=0;n<3;n++)if(p->s.consumers[n])value.consumer_mask|=1U<<n;
  value.worker_owned=p->s.lease.owned;value.service_owned=p->s.service_hold!=0;*(struct i9100_sensor_hid_status *)argument=value;return 0;}
 case SH_GET:{
  uint32_t kind=((hid_get_request *)argument)->kind;
  if(!l||l->owned||l->self||l->collector||l->kind||kind<1U||kind>3U||!outside(p,sizeof(*p),l,sizeof(*l)))return I9100_SH_ARGUMENT;
  unsigned slot=kind-1U;if(p->s.status.closing||p->s.status.closed||p->s.status.running||!(p->s.status.mask&(1U<<slot))||p->s.consumers[slot])return I9100_SH_BUSY;
  if(p->s.generations[slot]==0xffffffffU)return I9100_SH_OWNER;
  l->kind=kind;l->collector=p;l->self=(uintptr_t)l;l->generation=++p->s.generations[slot];l->owned=1;p->s.consumers[slot]=l;p->retain();return 0;}
 case SH_PUT:
  if(!consumer_valid(p,l))return I9100_SH_OWNER;if(p->s.status.running)return I9100_SH_BUSY;
  p->s.consumers[l->kind-1U]=0;l->owned=0;return 0;
 case SH_BIND:
  if(!consumer_valid(p,l))return I9100_SH_OWNER;
  if(p->s.status.running||p->s.status.closing||p->s.status.closed||p->s.status.produced)return I9100_SH_BUSY;
  p->s.profile.sender[l->kind-1U]=((hid_bind_request *)argument)->sender;return 0;
 case SH_HOLD_GET:{
  auto *hold=(i9100_sensor_hid_hold *)argument;
  if(!hold||hold->owned||hold->self||hold->collector||!outside(p,sizeof(*p),hold,sizeof(*hold)))return I9100_SH_ARGUMENT;
  if(p->s.service_hold||!p->s.status.ready||p->s.status.running||p->s.status.closing||p->s.status.closed)return I9100_SH_BUSY;
  if(p->s.service_generation==0xffffffffU)return I9100_SH_OWNER;
  hold->self=(uintptr_t)hold;hold->collector=p;hold->generation=++p->s.service_generation;hold->owned=1;p->s.service_hold=hold;p->retain();return 0;}
 case SH_HOLD_PUT:{
  auto *hold=(i9100_sensor_hid_hold *)argument;
  if(!hold||!hold->owned||hold->self!=(uintptr_t)hold||hold->collector!=p||p->s.service_hold!=hold||hold->generation!=p->s.service_generation)return I9100_SH_OWNER;
  if(p->s.status.running||!p->s.status.closed||p->s.lease.owned)return I9100_SH_BUSY;
  for(unsigned n=0;n<3;n++)if(p->s.consumers[n])return I9100_SH_BUSY;
  p->s.service_hold=0;hold->owned=0;return 0;}
 case SH_DETACH:{
  if(p->s.service_hold)return I9100_SH_BUSY;
  for(unsigned n=0;n<3;n++)if(p->s.consumers[n])return I9100_SH_BUSY;
  int e=halt(p,0);if(e)return e;
  if(p->s.added){e=p->s.loop->removeEventSource(p->s.timer);if(e){p->s.status.cleanup_error=e;return e;}p->s.added=0;}return 0;}
 default:return I9100_SH_ARGUMENT;
 }
}
void i9100_sensor_hid::free()
{if(s.timer)s.timer->release();if(s.loop)s.loop->release();OSObject::free();}
static int run(i9100_sensor_hid *p,unsigned op,void *arg=0,i9100_sensor_hid_consumer *l=0)
{
 if(!p||!p->s.loop||!p->s.timer||!ml_get_interrupts_enabled())return I9100_SH_ARGUMENT;
 if(p->s.loop->onThread()&&op!=SH_READ&&op!=SH_STATUS)return I9100_SH_BUSY;
 return p->s.loop->runAction(control,p,(void *)(uintptr_t)op,arg,l);
}
int i9100_sensor_hid_create(int enabled,i9100_sensor_worker *worker,const i9100_sensor_hid_profile *profile,i9100_sensor_hid **out)
{
 if(!out)return I9100_SH_ARGUMENT;if(*out)return I9100_SH_BUSY;if(enabled!=1)return I9100_SH_DISABLED;
 if(!worker||!profile||!profile->clock.absolute_ticks||!ml_get_interrupts_enabled()||profile->poll_us<1000U||profile->poll_us>20000U)return I9100_SH_ARGUMENT;
 struct i9100_sensor_worker_status state={};int e=i9100_sensor_worker_status(worker,&state);if(e)return e;
 if(!state.ready||state.running||state.closed||state.fault||state.consumer_owned)return I9100_SH_BUSY;
 for(unsigned n=0;n<3;n++)if((state.mask&(1U<<n))&&!profile->sender[n])return I9100_SH_ARGUMENT;
 auto *p=new i9100_sensor_hid;if(!p)return I9100_SH_FAULT;
 if(!p->initialize()){p->release();return I9100_SH_FAULT;}p->s.worker=worker;p->s.profile=*profile;p->s.status.mask=state.mask;
 p->s.loop=IOWorkLoop::workLoop();if(!p->s.loop){p->release();return I9100_SH_FAULT;}
 p->s.timer=IOTimerEventSource::timerEventSource(p,timer_action);if(!p->s.timer){p->release();return I9100_SH_FAULT;}
 p->s.timer->disable();e=p->s.loop->addEventSource(p->s.timer);if(!e)p->s.added=1;
 if(!e)e=i9100_sensor_worker_consumer_get(worker,&p->s.lease);
 if(e){int cleanup=p->s.loop->runAction(control,p,(void *)SH_DETACH);
  if(cleanup){p->s.status.error=e;p->s.status.fault=1;p->s.status.cleanup_error=cleanup;*out=p;}else p->release();return e;}
 p->s.status.ready=p->s.status.worker_owned=1;*out=p;return 0;
}
int i9100_sensor_hid_start(i9100_sensor_hid *p){return run(p,SH_START);}
int i9100_sensor_hid_stop(i9100_sensor_hid *p){return run(p,SH_STOP);}
int i9100_sensor_hid_recover_and_stop(i9100_sensor_hid *p){return run(p,SH_RECOVER);}
int i9100_sensor_hid_read(i9100_sensor_hid *p,uint32_t kind,i9100_sensor_hid_record *records,uint32_t capacity,uint32_t *received)
{
 if(kind<1U||kind>3U||!capacity||capacity>SH_CAPACITY||!outside(p,sizeof(*p),records,capacity*sizeof(*records))||
    !outside(p,sizeof(*p),received,sizeof(*received))||!outside(records,capacity*sizeof(*records),received,sizeof(*received)))return I9100_SH_ARGUMENT;
 hid_read_request r={kind,capacity,received,records};return run(p,SH_READ,&r);
}
int i9100_sensor_hid_status(i9100_sensor_hid *p,struct i9100_sensor_hid_status *out)
{return outside(p,sizeof(*p),out,sizeof(*out))?run(p,SH_STATUS,out):I9100_SH_ARGUMENT;}
int i9100_sensor_hid_destroy(i9100_sensor_hid **owner)
{if(!owner||!*owner)return I9100_SH_ARGUMENT;auto *p=*owner;int e=run(p,SH_DETACH);if(e)return e;p->release();*owner=0;return 0;}
int i9100_sensor_hid_consumer_get(i9100_sensor_hid *p,uint32_t kind,i9100_sensor_hid_consumer *l)
{if(!l||l->owned||l->self||l->collector||l->kind||kind<1U||kind>3U)return I9100_SH_ARGUMENT;
 hid_get_request r={kind};return run(p,SH_GET,&r,l);}
int i9100_sensor_hid_consumer_put(i9100_sensor_hid_consumer *l)
{if(!l||!l->owned||l->self!=(uintptr_t)l||!l->collector)return I9100_SH_OWNER;
 auto *p=l->collector;int e=run(p,SH_PUT,0,l);if(!e)p->release();return e;}
int i9100_sensor_hid_consumer_bind_sender(i9100_sensor_hid_consumer *l,uint64_t sender)
{if(!l||!l->owned||l->self!=(uintptr_t)l||!l->collector||!sender)return I9100_SH_ARGUMENT;
 hid_bind_request r={sender};return run(l->collector,SH_BIND,&r,l);}
int i9100_sensor_hid_consumer_read(i9100_sensor_hid_consumer *l,i9100_sensor_hid_record *records,uint32_t capacity,uint32_t *received)
{
 if(!l||!l->owned||l->self!=(uintptr_t)l||!l->collector||l->kind<1U||l->kind>3U||!capacity||capacity>SH_CAPACITY||
    !outside(l,sizeof(*l),records,capacity*sizeof(*records))||!outside(l,sizeof(*l),received,sizeof(*received))||
    !outside(l->collector,sizeof(*l->collector),records,capacity*sizeof(*records))||
    !outside(l->collector,sizeof(*l->collector),received,sizeof(*received))||
    !outside(records,capacity*sizeof(*records),received,sizeof(*received)))return I9100_SH_ARGUMENT;
 hid_read_request r={l->kind,capacity,received,records};return run(l->collector,SH_READ,&r,l);
}
int i9100_sensor_hid_hold_get(i9100_sensor_hid *p,i9100_sensor_hid_hold *hold)
{return run(p,SH_HOLD_GET,hold);}
int i9100_sensor_hid_hold_put(i9100_sensor_hid_hold *hold)
{if(!hold||!hold->owned||hold->self!=(uintptr_t)hold||!hold->collector)return I9100_SH_OWNER;
 auto *p=hold->collector;int e=run(p,SH_HOLD_PUT,hold);if(!e)p->release();return e;}
