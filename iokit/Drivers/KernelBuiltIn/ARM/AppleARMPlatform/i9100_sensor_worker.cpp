/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sensor_worker.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <string.h>
enum {CAPACITY=32,OPEN=1,START,STOP,RECOVER_STOP,READ,STATUS,DETACH};
struct sensor_worker_state {
 i9100_peripheral_native *parent;IOWorkLoop *loop;IOTimerEventSource *timer;
 i9100_sensor_worker_profile profile;i9100_sensor sensor[3];
 i9100_peripheral_i2c io[2];i9100_native_transport_lease bus[2];
 i9100_native_transport_hold worker[2],device[3];
 i9100_sensor_sample queue[CAPACITY];uint32_t head,count;
 uint32_t ready,running,stopping,fault,closed,added,busy;
 uint64_t busy_since[3],produced,consumed,dropped,not_ready,overflows,timer_ticks;
 int32_t error,cleanup_error;
};
struct i9100_sensor_worker : public OSObject {
 OSDeclareDefaultStructors(i9100_sensor_worker)
public:
 sensor_worker_state s;
 bool initialize(){memset(&s,0,sizeof(s));return OSObject::init();}
 virtual void free();
};
OSDefineMetaClassAndStructors(i9100_sensor_worker,OSObject)
static bool selected(i9100_sensor_worker *w,unsigned n){return !!(w->s.profile.mask&(1U<<n));}
static unsigned device_bus(unsigned n){return n==2U?1U:0U;}
static unsigned native_bus(unsigned n){return n==1U?2U:0U;}
static void inc(uint64_t *n){if(*n!=~(uint64_t)0)(*n)++;}
static bool separate(i9100_sensor_worker *w,const void *p,size_t n)
{uintptr_t a=(uintptr_t)w,b=(uintptr_t)p;return w&&p&&a<=~(uintptr_t)0-sizeof(*w)&&b<=~(uintptr_t)0-n&&!(a+sizeof(*w)>b&&b+n>a);}
static void sources_off(i9100_sensor_worker *w)
{w->s.running=0;if(w->s.timer){w->s.timer->disable();w->s.timer->cancelTimeout();}}
static void fault(i9100_sensor_worker *w,int error)
{sources_off(w);w->s.fault=1;if(!w->s.error)w->s.error=error;}
static int close_devices(i9100_sensor_worker *w,int recover)
{
 sources_off(w);w->s.stopping=1;int first=0;
 if(recover){
  for(unsigned b=0;b<2;b++)if(w->s.worker[b].owned){
   int state=i9100_peripheral_native_transport_hold_check(&w->s.worker[b]);
   if(state==I9100_GPIO_IO)state=i9100_peripheral_native_i2c_recover_owned(&w->s.bus[b]);
   if(state&&!first)first=state;
  }
  if(first){w->s.cleanup_error=first;fault(w,first);return first;}
 }
 for(unsigned remaining=3;remaining;remaining--){
  unsigned n=remaining-1;int e=0;
  if(w->s.sensor[n].initialized&&(w->s.sensor[n].active||w->s.sensor[n].fault))e=i9100_sensor_enable(&w->s.sensor[n],0);
  if(!e&&w->s.device[n].owned)e=i9100_peripheral_native_transport_hold_put(&w->s.device[n]);
  if(e&&!first)first=e;
 }
 for(unsigned b=0;b<2;b++){
  bool held=false;for(unsigned n=0;n<3;n++)if(device_bus(n)==b&&w->s.device[n].owned)held=true;
  if(held)continue;
  int e=0;if(w->s.worker[b].owned)e=i9100_peripheral_native_transport_hold_put(&w->s.worker[b]);
  if(!e&&w->s.bus[b].owned)e=i9100_peripheral_native_transport_close(&w->s.bus[b]);
  if(e&&!first)first=e;
 }
 if(first){w->s.cleanup_error=first;fault(w,first);return first;}
 w->s.closed=1;w->s.ready=0;w->s.parent=0;w->s.cleanup_error=0;return 0;
}
static int open_devices(i9100_sensor_worker *w)
{
 for(unsigned b=0;b<2;b++){
  bool use=b?!!(w->s.profile.mask&4U):!!(w->s.profile.mask&3U);if(!use)continue;
  int e=i9100_peripheral_native_i2c_open_owned(w->s.parent,&w->s.bus[b],native_bus(b),w->s.profile.pclk_hz[b],&w->s.io[b]);if(e)return e;
  e=i9100_peripheral_native_transport_hold_get(&w->s.bus[b],&w->s.worker[b]);if(e)return e;
 }
 for(unsigned n=0;n<3;n++)if(selected(w,n)){
  unsigned b=device_bus(n);int e=i9100_sensor_init(&w->s.sensor[n],&w->s.io[b],&w->s.profile.sensor[n]);if(e)return e;
  e=i9100_peripheral_native_transport_hold_get(&w->s.bus[b],&w->s.device[n]);if(e)return e;
  e=i9100_sensor_enable(&w->s.sensor[n],1);if(e)return e;
 }
 w->s.ready=1;return 0;
}
static void queue_sample(i9100_sensor_worker *w,const i9100_sensor_sample *sample)
{
 if(w->s.count==CAPACITY){w->s.head=(w->s.head+1U)%CAPACITY;w->s.count--;inc(&w->s.dropped);}
 w->s.queue[(w->s.head+w->s.count)%CAPACITY]=*sample;w->s.count++;inc(&w->s.produced);
}
static bool busy(i9100_sensor_worker *w,unsigned n,int e)
{
 if(e!=I9100_SENSOR_BUSY){w->s.busy&=~(1U<<n);return false;}
 unsigned b=device_bus(n);uint64_t now=w->s.io[b].now_ns(w->s.io[b].cookie);
 if(!(w->s.busy&(1U<<n))){w->s.busy|=1U<<n;w->s.busy_since[n]=now;}
 else if(now<w->s.busy_since[n]||now-w->s.busy_since[n]>500000000ULL)fault(w,I9100_SENSOR_TIMEOUT);
 return true;
}
static void process(i9100_sensor_worker *w)
{
 if(!w->s.running||w->s.stopping||w->s.fault)return;inc(&w->s.timer_ticks);
 for(unsigned n=0;n<3;n++)if(selected(w,n)){
  int e=i9100_peripheral_native_transport_hold_check(&w->s.device[n]);
  if(e==I9100_GPIO_BUSY){if(busy(w,n,I9100_SENSOR_BUSY)&&w->s.fault)return;continue;}
  if(e){fault(w,e);return;}
  if(n==2U&&!w->s.sensor[n].converting){
   e=i9100_sensor_begin(&w->s.sensor[n]);if(busy(w,n,e)){if(w->s.fault)return;continue;}
   if(e){fault(w,e);return;}continue;
  }
  i9100_sensor_sample sample;e=i9100_sensor_read(&w->s.sensor[n],&sample);
  if(busy(w,n,e)){if(w->s.fault)return;continue;}
  if(!e)queue_sample(w,&sample);
  else if(e==I9100_SENSOR_NOT_READY)inc(&w->s.not_ready);
  else if(e==I9100_SENSOR_OVERFLOW&&w->s.sensor[n].sequence!=0xffffffffU)inc(&w->s.overflows);
  else{fault(w,e);return;}
  if(n==2U&&!w->s.sensor[n].converting){
   e=i9100_sensor_begin(&w->s.sensor[n]);if(busy(w,n,e)){if(w->s.fault)return;continue;}
   if(e){fault(w,e);return;}
  }
 }
 if(w->s.timer->setTimeoutUS(w->s.profile.poll_us)!=kIOReturnSuccess)fault(w,I9100_SENSOR_IO);
}
static void timer_action(OSObject *owner,IOTimerEventSource *){process((i9100_sensor_worker *)owner);}
struct read_request {i9100_sensor_sample *samples;uint32_t capacity,*received;};
static IOReturn control(OSObject *owner,void *operation,void *argument,void *,void *)
{
 i9100_sensor_worker *w=(i9100_sensor_worker *)owner;
 switch((uintptr_t)operation){
 case OPEN:return open_devices(w);
 case START:
  if(!w->s.ready||w->s.running||w->s.stopping||w->s.fault||w->s.closed)return I9100_SENSOR_BUSY;
  w->s.running=1;w->s.timer->enable();
  if(w->s.timer->setTimeoutUS(w->s.profile.poll_us)!=kIOReturnSuccess){fault(w,I9100_SENSOR_IO);return I9100_SENSOR_IO;}return 0;
 case STOP:return close_devices(w,0);
 case RECOVER_STOP:
  if(!w->s.stopping&&!w->s.fault)return I9100_SENSOR_ARGUMENT;return close_devices(w,1);
 case READ:{
  read_request *r=(read_request *)argument;uint32_t count=w->s.count<r->capacity?w->s.count:r->capacity;
  for(unsigned n=0;n<count;n++)r->samples[n]=w->s.queue[(w->s.head+n)%CAPACITY];
  w->s.head=(w->s.head+count)%CAPACITY;w->s.count-=count;
  for(unsigned n=0;n<count;n++)inc(&w->s.consumed);*r->received=count;return 0;}
 case STATUS:{
  struct i9100_sensor_worker_status s;memset(&s,0,sizeof(s));
  s.ready=w->s.ready;s.running=w->s.running;s.stopping=w->s.stopping;s.fault=w->s.fault;s.closed=w->s.closed;
  s.mask=w->s.profile.mask;s.queued=w->s.count;s.capacity=CAPACITY;s.busy=w->s.busy;
  s.produced=w->s.produced;s.consumed=w->s.consumed;s.dropped=w->s.dropped;s.not_ready=w->s.not_ready;
  s.overflows=w->s.overflows;s.timer_ticks=w->s.timer_ticks;s.error=w->s.error;s.cleanup_error=w->s.cleanup_error;
  for(unsigned b=0;b<2;b++){if(w->s.bus[b].owned)s.bus_owned|=1U<<b;if(w->s.worker[b].owned)s.worker_holds|=1U<<b;}
  for(unsigned n=0;n<3;n++){
   if(w->s.device[n].owned)s.device_holds|=1U<<n;if(w->s.sensor[n].converting)s.converting|=1U<<n;
   s.active[n]=w->s.sensor[n].active;s.sensor_fault[n]=w->s.sensor[n].fault;
   s.sequence[n]=w->s.sensor[n].sequence;s.id[n]=w->s.sensor[n].id;
  }
  *(struct i9100_sensor_worker_status *)argument=s;return 0;}
 case DETACH:{int e=close_devices(w,0);if(e)return e;
  if(w->s.added){e=w->s.loop->removeEventSource(w->s.timer);if(e){w->s.cleanup_error=e;return e;}w->s.added=0;}return 0;}
 default:return I9100_SENSOR_ARGUMENT;
 }
}
void i9100_sensor_worker::free()
{if(s.timer)s.timer->release();if(s.loop)s.loop->release();OSObject::free();}
int i9100_sensor_worker_create(int enabled,i9100_peripheral_native *parent,const i9100_sensor_worker_profile *p,i9100_sensor_worker **out)
{
 if(!out)return I9100_SENSOR_ARGUMENT;if(*out)return I9100_SENSOR_BUSY;if(enabled!=1)return I9100_SENSOR_DISABLED;
 if(!parent||!p||!ml_get_interrupts_enabled()||!p->mask||(p->mask&~7U)||p->poll_us<1000U||p->poll_us>20000U||p->board_and_clock_verified!=1)return I9100_SENSOR_ARGUMENT;
 for(unsigned n=0;n<3;n++)if((p->mask&(1U<<n))&&(unsigned)p->sensor[n].kind!=n+1U)return I9100_SENSOR_ARGUMENT;
 for(unsigned b=0;b<2;b++)if((b?(p->mask&4U):(p->mask&3U))&&(p->pclk_hz[b]<1000000U||p->pclk_hz[b]>200000000U))return I9100_SENSOR_ARGUMENT;
 i9100_sensor_worker *w=new i9100_sensor_worker;if(!w)return I9100_SENSOR_IO;
 if(!w->initialize()){w->release();return I9100_SENSOR_IO;}w->s.parent=parent;w->s.profile=*p;
 w->s.loop=IOWorkLoop::workLoop();if(!w->s.loop){w->release();return I9100_SENSOR_IO;}
 w->s.timer=IOTimerEventSource::timerEventSource(w,timer_action);if(!w->s.timer){w->release();return I9100_SENSOR_IO;}
 w->s.timer->disable();int e=w->s.loop->addEventSource(w->s.timer);if(!e)w->s.added=1;
 if(!e)e=w->s.loop->runAction(control,w,(void *)OPEN);
 if(e){w->s.error=e;int cleanup=w->s.loop->runAction(control,w,(void *)DETACH);
  if(cleanup){w->s.cleanup_error=cleanup;w->s.fault=1;*out=w;}else w->release();return e;}
 *out=w;return 0;
}
static int run(i9100_sensor_worker *w,unsigned op,void *argument=0)
{
 if(!w||!w->s.loop||!w->s.timer||!ml_get_interrupts_enabled())return I9100_SENSOR_ARGUMENT;
 if(w->s.loop->onThread()&&op!=STATUS&&op!=READ)return I9100_SENSOR_BUSY;
 return w->s.loop->runAction(control,w,(void *)(uintptr_t)op,argument);
}
int i9100_sensor_worker_start(i9100_sensor_worker *w){return run(w,START);}
int i9100_sensor_worker_stop(i9100_sensor_worker *w){return run(w,STOP);}
int i9100_sensor_worker_recover_and_stop(i9100_sensor_worker *w){return run(w,RECOVER_STOP);}
int i9100_sensor_worker_read(i9100_sensor_worker *w,i9100_sensor_sample *samples,uint32_t capacity,uint32_t *received)
{
 if(!capacity||capacity>CAPACITY||!separate(w,samples,capacity*sizeof(*samples))||!separate(w,received,sizeof(*received)))return I9100_SENSOR_ARGUMENT;
 uintptr_t a=(uintptr_t)samples,b=(uintptr_t)received;
 if(a+capacity*sizeof(*samples)>b&&b+sizeof(*received)>a)return I9100_SENSOR_ARGUMENT;
 read_request r={samples,capacity,received};return run(w,READ,&r);
}
int i9100_sensor_worker_status(i9100_sensor_worker *w,struct i9100_sensor_worker_status *out)
{return separate(w,out,sizeof(*out))?run(w,STATUS,out):I9100_SENSOR_ARGUMENT;}
int i9100_sensor_worker_destroy(i9100_sensor_worker **out)
{
 if(!out||!*out)return I9100_SENSOR_ARGUMENT;i9100_sensor_worker *w=*out;
 int e=run(w,DETACH);if(e)return e;w->release();*out=0;return 0;
}
