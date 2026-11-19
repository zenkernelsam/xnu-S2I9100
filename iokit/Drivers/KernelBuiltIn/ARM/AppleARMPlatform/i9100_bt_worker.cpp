/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bt_worker.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <string.h>
struct bt_worker_state {
 i9100_radio_native *radio;IOWorkLoop *loop;IOTimerEventSource *timer;
 i9100_native_transport_hold session,hold;i9100_bt_worker_profile profile;
 i9100_bcm_fw_integrity integrity;i9100_bt_output output;i9100_bt_io native;
 i9100_bt_boot boot;uint8_t *hcd;uint32_t bytes,records,added,generation;
 i9100_bt_hcd_record *index;
 struct i9100_bt_worker_status status;
};
struct i9100_bt_worker : public OSObject {
 OSDeclareDefaultStructors(i9100_bt_worker)
public:
 bt_worker_state s;
 bool initialize(){memset(&s,0,sizeof(s));return OSObject::init();}
 virtual void free();
};
OSDefineMetaClassAndStructors(i9100_bt_worker,OSObject)
enum {BW_START=1,BW_COMMAND,BW_STOP,BW_STATUS,BW_DETACH};
static int wire_lock(void *cookie)
{auto *w=(i9100_bt_worker *)cookie;if(i9100_radio_native_verify_uart_cleanup(w->s.radio,w->s.generation))return -1;
 return w->s.native.lock(w->s.native.cookie);}
static void wire_unlock(void *cookie)
{auto *w=(i9100_bt_worker *)cookie;w->s.native.unlock(w->s.native.cookie);}
static int wire_write(void *cookie,const uint8_t *bytes,unsigned n,unsigned *sent)
{auto *w=(i9100_bt_worker *)cookie;return w->s.native.write(w->s.native.cookie,bytes,n,sent);}
static int wire_quiesce(void *cookie)
{auto *w=(i9100_bt_worker *)cookie;return w->s.native.quiesce(w->s.native.cookie);}
static uint64_t wire_time(void *cookie)
{auto *w=(i9100_bt_worker *)cookie;return w->s.native.now_ns(w->s.native.cookie);}
static int wire_read(void *cookie,uint8_t *bytes,unsigned n,unsigned *received)
{auto *w=(i9100_bt_worker *)cookie;
 if(i9100_radio_native_verify(w->s.radio,I9100_RADIO_BT,w->s.generation)){*received=0;return -1;}
 return i9100_radio_native_uart_read(w->s.radio,bytes,n,received);}
static int wire_verify(void *cookie,uint32_t generation)
{
 auto *w=(i9100_bt_worker *)cookie;
 if(generation!=w->s.generation||!w->s.status.uart_owned||!w->s.session.owned||!w->s.hold.owned)return -1;
 if(i9100_radio_native_verify(w->s.radio,I9100_RADIO_BT,generation))return -1;
 int e=i9100_peripheral_native_transport_hold_check(&w->s.session);
 return e?e:i9100_peripheral_native_transport_hold_check(&w->s.hold);
}
static int wire_sha(void *cookie,const void *p,uint32_t n,uint8_t digest[32])
{auto *w=(i9100_bt_worker *)cookie;return w->s.integrity.sha256(w->s.integrity.cookie,p,n,digest);}
static int wire_receive(void *cookie,const i9100_bt_frame *frame)
{auto *w=(i9100_bt_worker *)cookie;return w->s.output.receive(w->s.output.cookie,frame);}
static void sources_off(i9100_bt_worker *w)
{w->s.status.running=0;if(w->s.timer){w->s.timer->disable();w->s.timer->cancelTimeout();}}
static int halt(i9100_bt_worker *w)
{
 sources_off(w);w->s.status.closing=1;int e=0;
 if(w->s.boot.status.initialized)e=i9100_bt_boot_stop(&w->s.boot);
 if(e){w->s.status.cleanup_error=e;return e;}
 w->s.status.ready=0;return 0;
}
static void fault(i9100_bt_worker *w,int e)
{
 w->s.status.fault=1;if(!w->s.status.error)w->s.status.error=e;
 (void)halt(w);
}
static void process(i9100_bt_worker *w)
{
 if(!w->s.status.running||w->s.status.fault||w->s.status.closing)return;
 if(w->s.status.timer_ticks!=~(uint64_t)0)w->s.status.timer_ticks++;
 int e=i9100_bt_boot_step(&w->s.boot,4);if(e){fault(w,e);return;}
 w->s.status.ready=w->s.boot.status.ready;
 if(w->s.timer->setTimeoutUS(w->s.profile.poll_us)!=kIOReturnSuccess)fault(w,I9100_BT_BOOT_IO);
}
static void timer(OSObject *owner,IOTimerEventSource *){process((i9100_bt_worker *)owner);}
static int detach(i9100_bt_worker *w)
{
 int e=halt(w);if(e)return e;
 if(w->s.added){e=w->s.loop->removeEventSource(w->s.timer);if(e){w->s.status.cleanup_error=e;return e;}w->s.added=0;}
 if(w->s.status.uart_owned){e=i9100_radio_native_verify_uart_cleanup(w->s.radio,w->s.generation);
  if(e){w->s.status.cleanup_error=e;return e;}}
 if(w->s.hold.owned){e=i9100_peripheral_native_transport_hold_put(&w->s.hold);if(e){w->s.status.cleanup_error=e;return e;}}
 if(w->s.session.owned){e=i9100_peripheral_native_transport_hold_put(&w->s.session);if(e){w->s.status.cleanup_error=e;return e;}}
 if(w->s.status.uart_owned){e=i9100_radio_native_transport_close(w->s.radio,I9100_RADIO_BT);
  if(e){w->s.status.cleanup_error=e;return e;}w->s.status.uart_owned=0;}
 w->s.status.closed=1;w->s.status.cleanup_error=0;return 0;
}
struct command_request {uint16_t opcode;const uint8_t *bytes;unsigned count;};
static IOReturn action(OSObject *owner,void *operation,void *argument,void *,void *)
{
 auto *w=(i9100_bt_worker *)owner;
 switch((uintptr_t)operation){
 case BW_START:
  if(w->s.status.running||w->s.status.closing||w->s.status.closed||w->s.status.fault)return I9100_BT_BOOT_BUSY;
  {int e=i9100_bt_boot_start(&w->s.boot);if(e){fault(w,e);return e;}}
  w->s.status.running=1;w->s.timer->enable();
  if(w->s.timer->setTimeoutUS(w->s.profile.poll_us)!=kIOReturnSuccess){fault(w,I9100_BT_BOOT_IO);return I9100_BT_BOOT_IO;}return 0;
 case BW_COMMAND:{
  if(!w->s.status.running||!w->s.status.ready||w->s.status.closing||w->s.status.fault)return I9100_BT_BOOT_BUSY;
  auto *r=(command_request *)argument;int e=i9100_bt_boot_command(&w->s.boot,r->opcode,r->bytes,r->count);
  if(e&&e!=I9100_BT_BUSY&&e!=I9100_BT_ARGUMENT&&e!=I9100_BT_BOOT_ARGUMENT)fault(w,e);return e;}
 case BW_STOP:return halt(w);
 case BW_DETACH:return detach(w);
 case BW_STATUS:{auto s=w->s.status;s.session_hold=w->s.session.owned;s.worker_hold=w->s.hold.owned;
  s.poll_us=w->s.profile.poll_us;s.boot=w->s.boot.status;
  if(w->s.boot.status.initialized)(void)i9100_bt_boot_status(&w->s.boot,&s.boot);
  *(struct i9100_bt_worker_status *)argument=s;return 0;}
 default:return I9100_BT_BOOT_ARGUMENT;
 }
}
void i9100_bt_worker::free()
{
 if(s.timer)s.timer->release();if(s.loop)s.loop->release();
 if(s.index){memset(s.index,0,s.records*sizeof(*s.index));IOFree(s.index,s.records*sizeof(*s.index));}
 if(s.hcd){memset(s.hcd,0,s.bytes);IOFree(s.hcd,s.bytes);}OSObject::free();
}
static int run(i9100_bt_worker *w,unsigned op,void *argument=0)
{
 if(!w||!w->s.loop||!w->s.timer||!ml_get_interrupts_enabled())return I9100_BT_BOOT_ARGUMENT;
 if(w->s.loop->onThread()&&op!=BW_STATUS)return I9100_BT_BOOT_BUSY;
 return w->s.loop->runAction(action,w,(void *)(uintptr_t)op,argument);
}
int i9100_bt_worker_create(int enabled,i9100_radio_native *radio,const i9100_bt_worker_profile *p,
 const uint8_t *hcd,uint32_t bytes,const i9100_bcm_fw_integrity *integrity,const i9100_bt_output *out,i9100_bt_worker **owner)
{
 if(!owner)return I9100_BT_BOOT_ARGUMENT;if(*owner)return I9100_BT_BOOT_BUSY;if(enabled!=1)return I9100_BT_BOOT_DISABLED;
 if(!radio||!p||!integrity||!integrity->sha256||!out||!out->receive||!ml_get_interrupts_enabled()||p->poll_us<500U||p->poll_us>10000U||
    p->verified_uart_clock_hz<1000000U||p->verified_uart_clock_hz>200000000U||(p->fraction!=1&&p->fraction!=2))return I9100_BT_BOOT_ARGUMENT;
 uint32_t count=0;int e=i9100_bt_hcd_plan(hcd,bytes,&p->controller,&count);if(e)return e;
 i9100_radio_status status={};e=i9100_radio_native_status(radio,&status);if(e)return e;
 if(!status.ready||status.native_closing||!status.powered[1]||!status.generation[1]||status.native_uart_owned||
    status.generation[1]<=status.native_uart_epoch_used)return I9100_BT_BOOT_OWNER;
 auto *w=new i9100_bt_worker;if(!w)return I9100_BT_BOOT_IO;
 if(!w->initialize()){w->release();return I9100_BT_BOOT_IO;}w->s.radio=radio;w->s.profile=*p;w->s.integrity=*integrity;w->s.output=*out;w->s.bytes=bytes;w->s.records=count;w->s.generation=status.generation[1];
 w->s.hcd=(uint8_t *)IOMalloc(bytes);w->s.index=(i9100_bt_hcd_record *)IOMalloc(count*sizeof(*w->s.index));
 if(!w->s.hcd||!w->s.index){w->release();return I9100_BT_BOOT_IO;}memcpy(w->s.hcd,hcd,bytes);
 uint32_t audited=0;e=i9100_bt_hcd_audit(w->s.hcd,bytes,&p->controller,integrity->sha256,integrity->cookie,w->s.index,count,&audited);
 if(e||audited!=count){w->release();return e?e:I9100_BT_BOOT_LAYOUT;}
 w->s.loop=IOWorkLoop::workLoop();w->s.timer=IOTimerEventSource::timerEventSource(w,timer);
 if(!w->s.loop||!w->s.timer){w->release();return I9100_BT_BOOT_IO;}w->s.timer->disable();
 e=w->s.loop->addEventSource(w->s.timer);if(!e)w->s.added=1;
 if(!e)e=i9100_radio_native_uart_open(radio,p->verified_uart_clock_hz,115200U,p->fraction,&w->s.native);
 if(!e)w->s.status.uart_owned=1;
 else {i9100_radio_status after={};if(!i9100_radio_native_status(radio,&after))w->s.status.uart_owned=after.native_uart_owned;}
 if(!e)e=i9100_radio_native_borrow(radio,I9100_RADIO_BT,&w->s.session);
 if(!e)e=i9100_radio_native_borrow(radio,I9100_RADIO_BT,&w->s.hold);
 if(!e){i9100_bt_boot_io bio={{w,wire_lock,wire_unlock,wire_write,wire_quiesce,wire_time},w,wire_read,wire_verify,wire_sha,wire_receive};
  e=i9100_bt_boot_init(&w->s.boot,1,&bio,&p->controller,w->s.hcd,bytes,w->s.index,count,w->s.generation);}
 if(e){w->s.status.error=e;int cleanup=w->s.loop->runAction(action,w,(void *)BW_DETACH);
  if(cleanup){w->s.status.fault=1;w->s.status.cleanup_error=cleanup;*owner=w;}else w->release();return e;}
 *owner=w;return 0;
}
int i9100_bt_worker_start(i9100_bt_worker *w){return run(w,BW_START);}
int i9100_bt_worker_stop(i9100_bt_worker *w){return run(w,BW_STOP);}
int i9100_bt_worker_command(i9100_bt_worker *w,uint16_t op,const uint8_t *p,unsigned bytes)
{if(bytes){uintptr_t a=(uintptr_t)w,b=(uintptr_t)p;
  if(!w||!p||bytes>255U||a>~(uintptr_t)0-sizeof(*w)||b>~(uintptr_t)0-bytes||(a+sizeof(*w)>b&&b+bytes>a))return I9100_BT_BOOT_ARGUMENT;}
 command_request r={op,p,bytes};return run(w,BW_COMMAND,&r);}
int i9100_bt_worker_status(i9100_bt_worker *w,struct i9100_bt_worker_status *out)
{
 if(!w||!out)return I9100_BT_BOOT_ARGUMENT;uintptr_t a=(uintptr_t)w,b=(uintptr_t)out;
 if(a>~(uintptr_t)0-sizeof(*w)||b>~(uintptr_t)0-sizeof(*out)||(a+sizeof(*w)>b&&b+sizeof(*out)>a))return I9100_BT_BOOT_ARGUMENT;
 return run(w,BW_STATUS,out);
}
int i9100_bt_worker_destroy(i9100_bt_worker **owner)
{if(!owner||!*owner)return I9100_BT_BOOT_ARGUMENT;auto *w=*owner;uintptr_t a=(uintptr_t)w,b=(uintptr_t)owner;
 if(a>~(uintptr_t)0-sizeof(*w)||b>~(uintptr_t)0-sizeof(*owner)||(a+sizeof(*w)>b&&b+sizeof(*owner)>a))return I9100_BT_BOOT_ARGUMENT;
 int e=run(w,BW_DETACH);if(e)return e;w->release();*owner=0;return 0;}
