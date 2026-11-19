/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sensor_hid_endpoint.h"
#include "i9100_sensor_hid_queue.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <string.h>
extern "C" {
#include <mach/message.h>
#include <mach/mach_types.h>
}
enum {EP_START=1,EP_CLAIM,EP_UNCLAIM,EP_METHOD,EP_PORT,EP_MAP,EP_STATUS,EP_DETACH};
struct sensor_endpoint_state {
 IOWorkLoop *loop;IOTimerEventSource *timer;IOBufferMemoryDescriptor *buffer;
 uint8_t *bytes;i9100_sensor_hid_consumer consumer;void *client;uint32_t added;
 mach_msg_header_t notification;i9100_sensor_hid_packet last;
 struct i9100_sensor_hid_endpoint_status status;
};
struct i9100_sensor_hid_endpoint : public OSObject {
 OSDeclareDefaultStructors(i9100_sensor_hid_endpoint)
public:
 sensor_endpoint_state s;
 bool initialize(){memset(&s,0,sizeof(s));return OSObject::init();}
 virtual void free();
};
OSDefineMetaClassAndStructors(i9100_sensor_hid_endpoint,OSObject)
static void inc(uint64_t *n){if(*n!=~(uint64_t)0)(*n)++;}
static void barrier(void *){OSMemoryBarrier();}
static void copy(void *,void *out,const void *in,uint32_t n){memcpy(out,in,n);}
static void sources_off(i9100_sensor_hid_endpoint *p)
{p->s.status.running=0;if(p->s.timer){p->s.timer->disable();p->s.timer->cancelTimeout();}}
static void fault(i9100_sensor_hid_endpoint *p,int e)
{sources_off(p);p->s.status.closing=1;if(!p->s.status.error)p->s.status.error=e;}
static bool sane(i9100_sensor_hid_endpoint *p)
{uint32_t *q=(uint32_t *)p->s.bytes;return p->s.bytes&&q[0]==4096U&&q[2]==p->s.status.expected_tail&&q[1]<=4096U&&!(q[1]&63U);}
static void reset_queue(i9100_sensor_hid_endpoint *p)
{
 uint32_t *q=(uint32_t *)p->s.bytes;q[0]=4096;q[1]=q[2]=0;p->s.status.expected_tail=0;
 OSMemoryBarrier();
}
static void notify(i9100_sensor_hid_endpoint *p)
{
 if(!p->s.notification.msgh_remote_port){inc(&p->s.status.no_port);return;}
 kern_return_t e=mach_msg_send_from_kernel_proper(&p->s.notification,sizeof(p->s.notification));
 if(e==MACH_MSG_SUCCESS)inc(&p->s.status.notify_ok);
 else if(e==MACH_SEND_TIMED_OUT)inc(&p->s.status.notify_busy);
 else inc(&p->s.status.notify_error); /* Record once; don't replay an accepted packet. */
}
static void process(i9100_sensor_hid_endpoint *p)
{
 if(!p->s.status.running||p->s.status.closing||p->s.status.closed)return;
 struct i9100_sensor_hid_status collector={};int e=i9100_sensor_hid_status(p->s.consumer.collector,&collector);
 if(e||collector.fault||collector.closing||collector.closed){fault(p,e?e:collector.error?collector.error:I9100_SH_FAULT);return;}
 i9100_sensor_hid_record records[8];uint32_t count=0;
 e=i9100_sensor_hid_consumer_read(&p->s.consumer,records,8,&count);if(e){fault(p,e);return;}
 for(unsigned n=0;n<count;n++){
  p->s.last=records[n].packet;p->s.status.last_valid=1;
  if(!p->s.status.reader_open){inc(&p->s.status.dropped);continue;}
  if(!sane(p)){for(unsigned k=n;k<count;k++)inc(&p->s.status.dropped);fault(p,I9100_SH_OWNER);return;}
  uint32_t selected=0,tail=0;uint32_t *q=(uint32_t *)p->s.bytes;
  i9100_sensor_hid_queue_io io={p,barrier,copy};
  e=i9100_sensor_hid_queue_push(q+1,q+2,p->s.bytes+12,4096,p->s.status.expected_tail,&records[n].packet,&io,&selected,&tail);
  if(e==I9100_SH_BUSY){inc(&p->s.status.dropped);continue;}
  if(e){for(unsigned k=n;k<count;k++)inc(&p->s.status.dropped);fault(p,e);return;}
  p->s.status.expected_tail=tail;inc(&p->s.status.forwarded);if(selected)notify(p);
 }
 if(p->s.timer->setTimeoutUS(10000)!=kIOReturnSuccess)fault(p,I9100_SH_FAULT);
}
static void timer_action(OSObject *owner,IOTimerEventSource *){process((i9100_sensor_hid_endpoint *)owner);}
struct endpoint_method {void *client;uint32_t selector;IOExternalMethodArguments *args;};
struct endpoint_port {void *client;mach_port_t port;UInt32 type;};
struct endpoint_map {void *client;UInt32 type;IOOptionBits *options;IOMemoryDescriptor **memory;};
static IOReturn method(i9100_sensor_hid_endpoint *p,endpoint_method *r)
{
 auto *a=r->args;
 if(!a||p->s.client!=r->client||!r->client||a->version!=kIOExternalMethodArgumentsCurrentVersion||
    a->asyncWakePort||a->asyncReferenceCount||a->structureInputDescriptor||a->structureOutputDescriptor||
    a->structureVariableOutputData||a->scalarOutputCount)return kIOReturnBadArgument;
 if(r->selector<=1U&&a->scalarInputCount==1U&&a->scalarInput&&a->scalarInput[0]==0&&
    !a->structureInputSize&&!a->structureOutputSize){
  if(r->selector==0U){if(p->s.status.closing||p->s.status.closed)return kIOReturnNotReady;
   if(!p->s.status.reader_open){reset_queue(p);p->s.status.reader_open=1;}}
  else {p->s.status.reader_open=0;reset_queue(p);}return 0;
 }
 if(r->selector==2U&&a->scalarInputCount==2U&&a->scalarInput&&!a->scalarInput[1]&&
    !a->structureInputSize&&a->structureOutput){
  uint32_t type=0,page=0,usage=0;(void)i9100_sensor_hid_usage(p->s.status.kind,&type,&page,&usage);
  if(a->scalarInput[0]!=type||!p->s.status.last_valid)return kIOReturnNotFound;
  if(a->structureOutputSize<60U)return kIOReturnNoSpace;
  memcpy(a->structureOutput,p->s.last.bytes,60);a->structureOutputSize=60;return 0;
 }
 return kIOReturnUnsupported;
}
static IOReturn control(OSObject *owner,void *operation,void *argument,void *,void *)
{
 auto *p=(i9100_sensor_hid_endpoint *)owner;
 switch((uintptr_t)operation){
 case EP_START:
  if(!p->s.status.ready||p->s.status.running||p->s.status.closing||p->s.status.closed)return I9100_SH_BUSY;
  p->s.status.running=1;p->s.timer->enable();if(p->s.timer->setTimeoutUS(10000)!=kIOReturnSuccess){fault(p,I9100_SH_FAULT);return I9100_SH_FAULT;}return 0;
 case EP_CLAIM:
  if(!argument||p->s.client||p->s.status.closing||p->s.status.closed)return I9100_SH_BUSY;
  p->s.client=argument;p->s.status.claimed=1;return 0;
 case EP_UNCLAIM:
  if(!argument||p->s.client!=argument)return I9100_SH_OWNER;
  p->s.status.reader_open=0;p->s.client=0;p->s.status.claimed=0;
  p->s.notification.msgh_remote_port=MACH_PORT_NULL;reset_queue(p);return 0;
 case EP_METHOD:return method(p,(endpoint_method *)argument);
 case EP_PORT:{
  auto *r=(endpoint_port *)argument;if(p->s.client!=r->client||!r->client)return kIOReturnNotAttached;
  if(r->type)return kIOReturnUnsupported;p->s.notification.msgh_remote_port=r->port;
  if(p->s.status.reader_open&&((uint32_t *)p->s.bytes)[1]!=p->s.status.expected_tail)notify(p);return 0;}
 case EP_MAP:{
  auto *r=(endpoint_map *)argument;if(p->s.client!=r->client||!r->client)return kIOReturnNotAttached;
  if(r->type||!r->options||!r->memory)return kIOReturnBadArgument;
  p->s.buffer->retain();*r->options=0;*r->memory=p->s.buffer;return 0;}
 case EP_STATUS:*(struct i9100_sensor_hid_endpoint_status *)argument=p->s.status;return 0;
 case EP_DETACH:{
  if(p->s.client)return I9100_SH_BUSY;sources_off(p);p->s.status.closing=1;
  if(p->s.added){int e=p->s.loop->removeEventSource(p->s.timer);if(e){p->s.status.cleanup_error=e;return e;}p->s.added=0;}
  if(p->s.consumer.owned){int e=i9100_sensor_hid_consumer_put(&p->s.consumer);if(e){p->s.status.cleanup_error=e;return e;}}
  p->s.status.ready=0;p->s.status.closed=1;p->s.status.cleanup_error=0;return 0;}
 default:return I9100_SH_ARGUMENT;
 }
}
void i9100_sensor_hid_endpoint::free()
{if(s.timer)s.timer->release();if(s.loop)s.loop->release();if(s.buffer)s.buffer->release();OSObject::free();}
static int run(i9100_sensor_hid_endpoint *p,unsigned op,void *arg=0)
{
 if(!p||!p->s.loop||!p->s.timer||!ml_get_interrupts_enabled())return I9100_SH_ARGUMENT;
 if(p->s.loop->onThread()&&op!=EP_STATUS)return I9100_SH_BUSY;
 return p->s.loop->runAction(control,p,(void *)(uintptr_t)op,arg);
}
int i9100_sensor_hid_endpoint_create(int enabled,i9100_sensor_hid *collector,uint32_t kind,uint64_t sender,i9100_sensor_hid_endpoint **out)
{
 if(!out)return I9100_SH_ARGUMENT;if(*out)return I9100_SH_BUSY;if(enabled!=1)return I9100_SH_DISABLED;
 if(!collector||kind<1U||kind>3U||!sender||!ml_get_interrupts_enabled())return I9100_SH_ARGUMENT;
 auto *p=new i9100_sensor_hid_endpoint;if(!p)return I9100_SH_FAULT;
 if(!p->initialize()){p->release();return I9100_SH_FAULT;}
 p->s.status.kind=kind;p->s.loop=IOWorkLoop::workLoop();p->s.timer=IOTimerEventSource::timerEventSource(p,timer_action);
 p->s.buffer=IOBufferMemoryDescriptor::withCapacity(8192,kIODirectionInOut,false);
 p->s.bytes=p->s.buffer?(uint8_t *)p->s.buffer->getBytesNoCopy():0;
 if(!p->s.loop||!p->s.timer||!p->s.bytes){p->release();return I9100_SH_FAULT;}
 memset(p->s.bytes,0,8192);reset_queue(p);p->s.timer->disable();
 p->s.notification.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);p->s.notification.msgh_size=sizeof(p->s.notification);
 int e=p->s.loop->addEventSource(p->s.timer);if(!e)p->s.added=1;
 if(!e)e=i9100_sensor_hid_consumer_get(collector,kind,&p->s.consumer);
 if(!e)e=i9100_sensor_hid_consumer_bind_sender(&p->s.consumer,sender);
 if(e){int cleanup=p->s.loop->runAction(control,p,(void *)EP_DETACH);
  if(cleanup){p->s.status.error=e;p->s.status.cleanup_error=cleanup;*out=p;}else p->release();return e;}
 p->s.status.ready=1;*out=p;return 0;
}
int i9100_sensor_hid_endpoint_start(i9100_sensor_hid_endpoint *p){return run(p,EP_START);}
int i9100_sensor_hid_endpoint_claim(i9100_sensor_hid_endpoint *p,void *client){return run(p,EP_CLAIM,client);}
int i9100_sensor_hid_endpoint_unclaim(i9100_sensor_hid_endpoint *p,void *client){return run(p,EP_UNCLAIM,client);}
IOReturn i9100_sensor_hid_endpoint_method(i9100_sensor_hid_endpoint *p,void *c,uint32_t s,IOExternalMethodArguments *a)
{endpoint_method r={c,s,a};return run(p,EP_METHOD,&r);}
IOReturn i9100_sensor_hid_endpoint_port(i9100_sensor_hid_endpoint *p,void *c,mach_port_t port,UInt32 type)
{endpoint_port r={c,port,type};return run(p,EP_PORT,&r);}
IOReturn i9100_sensor_hid_endpoint_map(i9100_sensor_hid_endpoint *p,void *c,UInt32 t,IOOptionBits *o,IOMemoryDescriptor **m)
{endpoint_map r={c,t,o,m};return run(p,EP_MAP,&r);}
int i9100_sensor_hid_endpoint_status(i9100_sensor_hid_endpoint *p,struct i9100_sensor_hid_endpoint_status *out)
{if(!p||!out)return I9100_SH_ARGUMENT;uintptr_t a=(uintptr_t)p,b=(uintptr_t)out;
 if(a>~(uintptr_t)0-sizeof(*p)||b>~(uintptr_t)0-sizeof(*out)||(a+sizeof(*p)>b&&b+sizeof(*out)>a))return I9100_SH_ARGUMENT;return run(p,EP_STATUS,out);}
int i9100_sensor_hid_endpoint_destroy(i9100_sensor_hid_endpoint **owner)
{if(!owner||!*owner)return I9100_SH_ARGUMENT;auto *p=*owner;int e=run(p,EP_DETACH);if(e)return e;p->release();*owner=0;return 0;}
