/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_haptic_worker.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
enum {OPEN=1,PULSE,STOP,STATUS,DETACH};
struct haptic_state {
 i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;
 i9100_power_haptic_client power;i9100_power_haptic_hold power_hold;
 i9100_gpio_lease pin;i9100_clock_lease gate;i9100_clock_gates *clocks;
 IOMemoryDescriptor *memory;IOMemoryMap *map;volatile uint8_t *registers;
 IOLock *operation,*controller;IOWorkLoop *loop;IOTimerEventSource *timer;
 i9100_pwm_haptic pwm;i9100_vibrator motor;i9100_haptic_worker_profile profile;
 uint32_t ready,stopping,closed,fault,added,pulses,timer_events,clock_uncertain,pin_changed;int32_t error,cleanup_error;
};
struct i9100_haptic_worker : public OSObject {
 OSDeclareDefaultStructors(i9100_haptic_worker)
public:
 haptic_state s;bool initialize(){memset(&s,0,sizeof(s));return OSObject::init();}virtual void free();
};
OSDefineMetaClassAndStructors(i9100_haptic_worker,OSObject)
static uint64_t now_ns(void *){uint64_t n=0;absolutetime_to_nanoseconds(mach_absolute_time(),&n);return n;}
static int op_lock(void *c){auto *w=(i9100_haptic_worker *)c;return !ml_get_interrupts_enabled()||!IOLockTryLock(w->s.operation)?-1:0;}
static void op_unlock(void *c){IOLockUnlock(((i9100_haptic_worker *)c)->s.operation);}
static int controller_lock(void *c){auto *w=(i9100_haptic_worker *)c;return !ml_get_interrupts_enabled()||!IOLockTryLock(w->s.controller)?-1:0;}
static void controller_unlock(void *c){IOLockUnlock(((i9100_haptic_worker *)c)->s.controller);}
static uint32_t read_pwm(void *c,unsigned off)
{auto *w=(i9100_haptic_worker *)c;OSSynchronizeIO();uint32_t v=*(volatile uint32_t *)(w->s.registers+off);OSSynchronizeIO();return v;}
static void write_pwm(void *c,unsigned off,uint32_t v)
{auto *w=(i9100_haptic_worker *)c;OSSynchronizeIO();*(volatile uint32_t *)(w->s.registers+off)=v;OSSynchronizeIO();}
static int read_motor(void *c,uint8_t reg,uint8_t *v)
{return reg==2U?i9100_power_haptic_read(&((i9100_haptic_worker *)c)->s.power,v):I9100_SHARED_ARGUMENT;}
static int write_motor(void *c,uint8_t reg,uint8_t v)
{return reg==2U?i9100_power_haptic_write(&((i9100_haptic_worker *)c)->s.power,v):I9100_SHARED_ARGUMENT;}
static int rail(void *c,int enabled){return i9100_power_haptic_rail(&((i9100_haptic_worker *)c)->s.power,enabled);}
static void cancel(void *c){auto *w=(i9100_haptic_worker *)c;if(w->s.timer)w->s.timer->cancelTimeout();}
static int arm_timeout(void *c,uint32_t ms)
{auto *w=(i9100_haptic_worker *)c;if(!ms||ms>250U||w->s.stopping)return I9100_VIB_ARGUMENT;
 w->s.timer->enable();return w->s.timer->setTimeoutUS(ms*1000U)==kIOReturnSuccess?0:I9100_VIB_IO;}
static int idle_motor(i9100_haptic_worker *w)
{uint8_t config=0;int e=i9100_power_haptic_read(&w->s.power,&config);return e?e:((config&0x40U)?I9100_VIB_BUSY:0);}
static int pwm_run(void *c,int enabled)
{
 auto *w=(i9100_haptic_worker *)c;
 if(!w->s.registers||!w->s.gate.active)return enabled?I9100_PWM_DISABLED:0;
 int clock=i9100_clock_check(&w->s.gate);if(clock)return clock;
 if(!w->s.pwm.prepared)return enabled?I9100_PWM_DISABLED:((read_pwm(w,8)&0x300U)?I9100_PWM_BUSY:0);
 if(enabled){
  int e=i9100_power_haptic_hold_check(&w->s.power_hold);if(e)return e;e=i9100_clock_check(&w->s.gate);if(e)return e;
  uint8_t config=0;e=i9100_power_haptic_read(&w->s.power,&config);if(e)return e;if(config!=0xc3U)return I9100_VIB_BUSY;
 }
 return i9100_pwm_haptic_run(&w->s.pwm,enabled);
}
static int pwm_config(void *c,uint32_t period,uint32_t duty)
{
 auto *w=(i9100_haptic_worker *)c;if(w->s.stopping||w->s.fault||w->s.pwm.running)return I9100_PWM_BUSY;
 int e=i9100_power_haptic_hold_check(&w->s.power_hold);if(e)return e;e=i9100_clock_check(&w->s.gate);if(e)return e;
 e=idle_motor(w);if(e)return e;e=i9100_gpio_input(&w->s.pin,1);if(e)return e;
 if(w->s.pwm.prepared){e=i9100_pwm_haptic_release(&w->s.pwm);if(e)return e;}
 e=i9100_pwm_haptic_configure(&w->s.pwm,period,duty,1);if(e)return e;
 return i9100_gpio_function(&w->s.pin,1,2);
}
static void stop_sources(i9100_haptic_worker *w)
{if(w->s.timer){w->s.timer->disable();w->s.timer->cancelTimeout();}}
static void record_fault(i9100_haptic_worker *w,int e)
{w->s.fault=1;if(!w->s.error)w->s.error=e;stop_sources(w);}
static int stop_motor(i9100_haptic_worker *w)
{
 stop_sources(w);
 if(!w->s.motor.initialized||(!w->s.motor.enabled&&!w->s.motor.fault&&!w->s.motor.rail_owned))return 0;
 int e=i9100_vibrator_stop(&w->s.motor);if(e)w->s.cleanup_error=e;return e;
}
static int close_resources(i9100_haptic_worker *w)
{
 w->s.stopping=1;stop_sources(w);int e=0;
 if(w->s.motor.initialized&&(w->s.motor.enabled||w->s.motor.fault||w->s.motor.rail_owned)){
  e=i9100_vibrator_enable(&w->s.motor,0);if(e){w->s.cleanup_error=e;return e;}}
 if(w->s.power.ready){e=idle_motor(w);if(e)return e;}
 if(w->s.pwm.prepared){
  e=i9100_gpio_input(&w->s.pin,1);if(e)return e;e=i9100_pwm_haptic_release(&w->s.pwm);if(e)return e;
 }
 if(w->s.pin.active){e=i9100_gpio_release(&w->s.pin,w->s.pin_changed?I9100_GPIO_RESTORE:I9100_GPIO_KEEP);if(e)return e;}
 if(w->s.gate.active){e=i9100_clock_release(&w->s.gate);if(e)return e;}
 else if(w->s.clock_uncertain){e=i9100_clock_recover_idle(w->s.clocks,I9100_GATE_PWM);if(e)return e;w->s.clock_uncertain=0;}
 if(w->s.map){w->s.map->release();w->s.map=0;}if(w->s.memory){w->s.memory->release();w->s.memory=0;}w->s.registers=0;
 if(w->s.power_hold.owned){e=i9100_power_haptic_hold_put(&w->s.power_hold);if(e)return e;}
 if(w->s.power.owned){e=i9100_power_haptic_close(&w->s.power);if(e)return e;}
 if(w->s.parent_hold.owned){e=i9100_peripheral_native_hold_put(&w->s.parent_hold);if(e)return e;}
 w->s.parent=0;w->s.clocks=0;w->s.ready=0;w->s.closed=1;w->s.cleanup_error=0;return 0;
}
static int open_resources(i9100_haptic_worker *w)
{
 int e=i9100_peripheral_native_hold_get(w->s.parent,&w->s.parent_hold);if(e)return e;
 e=i9100_power_haptic_open(&w->s.power,1);if(e)return e;e=i9100_power_haptic_hold_get(&w->s.power,&w->s.power_hold);if(e)return e;
 unsigned irq=0;e=i9100_peripheral_native_irq_is_enabled(w->s.parent,70,&irq);if(e)return e;if(irq)return I9100_PWM_BUSY;
 w->s.memory=IOMemoryDescriptor::withPhysicalAddress(0x139d0000U,0x1000U,kIODirectionInOut);if(!w->s.memory)return I9100_PWM_IO;
 w->s.map=w->s.memory->map(kIOMapInhibitCache);if(!w->s.map)return I9100_PWM_IO;
 w->s.registers=(volatile uint8_t *)w->s.map->getAddress();if(!w->s.registers)return I9100_PWM_IO;
 auto *gpio=i9100_peripheral_native_gpio(w->s.parent);w->s.clocks=i9100_peripheral_native_clocks(w->s.parent);
 e=i9100_gpio_claim(gpio,I9100_GPD0,2U,&w->s.pin);if(e)return e;
 unsigned function=(w->s.pin.saved[0]>>4)&15U;if(function!=0U&&function!=2U)return I9100_PWM_BUSY;
 e=i9100_clock_acquire(w->s.clocks,I9100_GATE_PWM,&w->s.gate);
 if(e){w->s.clock_uncertain=e==I9100_CLOCK_IO;return e;}
 if((read_pwm(w,8)&0x300U)||(read_pwm(w,0x44)&2U))return I9100_PWM_BUSY;
 w->s.pin_changed=1;e=i9100_gpio_input(&w->s.pin,1);if(e)return e;e=i9100_gpio_pull(&w->s.pin,1,0);if(e)return e;
 e=i9100_gpio_drive(&w->s.pin,1,0);if(e)return e;
 i9100_pwm_io pio={w,read_pwm,write_pwm,controller_lock,controller_unlock};
 e=i9100_pwm_haptic_init(&w->s.pwm,&pio,w->s.profile.pclk_hz);if(e)return e;e=i9100_pwm_haptic_enable(&w->s.pwm,1);if(e)return e;
 i9100_vibrator_io vio={w,op_lock,op_unlock,read_motor,write_motor,rail,pwm_config,pwm_run,arm_timeout,cancel,now_ns};
 e=i9100_vibrator_init(&w->s.motor,&vio,38022U);if(e)return e;e=i9100_vibrator_enable(&w->s.motor,1);if(e)return e;
 w->s.ready=1;return 0;
}
static void timer_action(OSObject *object,IOTimerEventSource *)
{
 auto *w=(i9100_haptic_worker *)object;if(w->s.stopping||w->s.fault||!w->s.ready)return;w->s.timer_events++;
 int e=i9100_power_haptic_hold_check(&w->s.power_hold);if(!e)e=i9100_vibrator_timeout(&w->s.motor);
 if(e){record_fault(w,e);int off=stop_motor(w);if(off)w->s.cleanup_error=off;}
}
struct pulse_request {uint32_t ms;unsigned amplitude;};
static IOReturn control(OSObject *object,void *operation,void *argument,void *,void *)
{
 auto *w=(i9100_haptic_worker *)object;
 switch((uintptr_t)operation){
 case OPEN:return open_resources(w);
 case PULSE:{
  if(!w->s.ready||w->s.stopping||w->s.closed||w->s.fault)return I9100_VIB_DISABLED;
  int e=i9100_power_haptic_hold_check(&w->s.power_hold);if(e){record_fault(w,e);return e;}
  auto *p=(pulse_request *)argument;e=i9100_vibrator_pulse(&w->s.motor,p->ms,p->amplitude);
  if(e&&e!=I9100_VIB_ARGUMENT&&e!=I9100_VIB_BUSY)record_fault(w,e);if(!e)w->s.pulses++;return e;}
 case STOP:{int e=stop_motor(w);if(e)record_fault(w,e);return e;}
 case STATUS:{
  struct i9100_haptic_worker_status s;memset(&s,0,sizeof(s));s.ready=w->s.ready;s.stopping=w->s.stopping;s.closed=w->s.closed;s.fault=w->s.fault;
  s.active=w->s.motor.active;s.pwm_prepared=w->s.pwm.prepared;s.pwm_running=w->s.pwm.running;s.pwm_fault=w->s.pwm.fault;
  s.parent_held=w->s.parent_hold.owned;s.pin_owned=w->s.pin.active;s.clock_owned=w->s.gate.active;s.power_owned=w->s.power.owned;s.power_held=w->s.power_hold.owned;
  s.period_ns=w->s.pwm.actual_period_ns;s.duty_ns=w->s.pwm.actual_duty_ns;s.timer_events=w->s.timer_events;s.pulses=w->s.pulses;s.deadline_ns=w->s.motor.deadline;
  s.error=w->s.error;s.cleanup_error=w->s.cleanup_error;*(struct i9100_haptic_worker_status *)argument=s;return 0;}
 case DETACH:{int e=close_resources(w);if(e){w->s.cleanup_error=e;return e;}
  if(w->s.added){e=w->s.loop->removeEventSource(w->s.timer);if(e){w->s.cleanup_error=e;return e;}w->s.added=0;}return 0;}
 default:return I9100_VIB_ARGUMENT;
 }
}
void i9100_haptic_worker::free()
{if(s.timer)s.timer->release();if(s.loop)s.loop->release();if(s.operation)IOLockFree(s.operation);if(s.controller)IOLockFree(s.controller);OSObject::free();}
int i9100_haptic_worker_create(int enabled,i9100_peripheral_native *parent,const i9100_haptic_worker_profile *p,i9100_haptic_worker **out)
{
 if(!out)return I9100_VIB_ARGUMENT;if(*out)return I9100_VIB_BUSY;if(enabled!=1)return I9100_VIB_DISABLED;
 if(!parent||!p||!ml_get_interrupts_enabled()||p->board_and_clock_verified!=1U||p->pclk_hz<1000000U||p->pclk_hz>200000000U)return I9100_VIB_ARGUMENT;
 auto *w=new i9100_haptic_worker;if(!w)return I9100_VIB_IO;if(!w->initialize()){w->release();return I9100_VIB_IO;}
 w->s.parent=parent;w->s.profile=*p;w->s.operation=IOLockAlloc();w->s.controller=IOLockAlloc();w->s.loop=IOWorkLoop::workLoop();
 if(!w->s.operation||!w->s.controller||!w->s.loop){w->release();return I9100_VIB_IO;}
 w->s.timer=IOTimerEventSource::timerEventSource(w,timer_action);if(!w->s.timer){w->release();return I9100_VIB_IO;}
 w->s.timer->disable();int e=w->s.loop->addEventSource(w->s.timer);if(!e)w->s.added=1;
 if(!e)e=w->s.loop->runAction(control,w,(void *)OPEN);
 if(e){w->s.error=e;int off=w->s.loop->runAction(control,w,(void *)DETACH);
  if(off){w->s.cleanup_error=off;w->s.fault=1;*out=w;}else w->release();return e;}
 *out=w;return 0;
}
static bool separate(i9100_haptic_worker *w,const void *out,size_t n)
{uintptr_t a=(uintptr_t)w,b=(uintptr_t)out;return w&&out&&a<=~(uintptr_t)0-sizeof(*w)&&b<=~(uintptr_t)0-n&&!(a+sizeof(*w)>b&&b+n>a);}
static int run(i9100_haptic_worker *w,unsigned operation,void *argument=0)
{if(!w||!w->s.loop||!w->s.timer||!ml_get_interrupts_enabled())return I9100_VIB_ARGUMENT;
 if(w->s.loop->onThread()&&operation!=STATUS)return I9100_VIB_BUSY;
 return w->s.loop->runAction(control,w,(void *)(uintptr_t)operation,argument);}
int i9100_haptic_worker_pulse(i9100_haptic_worker *w,uint32_t ms,unsigned amplitude)
{if(!ms||ms>250U||!amplitude||amplitude>100U)return I9100_VIB_ARGUMENT;pulse_request p={ms,amplitude};return run(w,PULSE,&p);}
int i9100_haptic_worker_stop(i9100_haptic_worker *w){return run(w,STOP);}
int i9100_haptic_worker_status(i9100_haptic_worker *w,struct i9100_haptic_worker_status *out)
{return separate(w,out,sizeof(*out))?run(w,STATUS,out):I9100_VIB_ARGUMENT;}
int i9100_haptic_worker_destroy(i9100_haptic_worker **out)
{if(!out||!*out)return I9100_VIB_ARGUMENT;auto *w=*out;int e=run(w,DETACH);if(e)return e;w->release();*out=0;return 0;}
