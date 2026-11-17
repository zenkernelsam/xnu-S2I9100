/* SPDX-License-Identifier: BSD-2-Clause; actual native/Power/PWM/vibrator/workloop. */
#include "i9100_haptic_native_model.h"
#include "i9100_haptic_worker.h"
static struct i9100_haptic_worker_status get_status(i9100_haptic_worker *w)
{struct i9100_haptic_worker_status s;assert(!i9100_haptic_worker_status(w,&s));return s;}
struct Supply {
 HapticHardware hardware;i9100_power_hw hw;i9100_power_haptic_ops ops;bool dead=false;HapticOwner *owner;
 Supply():hw(hardware.io()),ops(hardware.ops()),owner(new HapticOwner(&dead)){pwm_power=&hardware;assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));}
 void close(){assert(!i9100_power_shared_unpublish(owner));owner->release();assert(dead);}
};
static i9100_haptic_worker_profile profile(){i9100_haptic_worker_profile p={100000000U,1};return p;}
static void parent_reference()
{
 auto *c=haptic_parent();i9100_native_parent_hold h={},copy={};assert(!i9100_peripheral_native_hold_get(c,&h));copy=h;
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 assert(i9100_peripheral_native_hold_put(&copy)==I9100_GPIO_OWNER);
 assert(!i9100_peripheral_native_hold_put(&h)&&i9100_peripheral_native_hold_put(&h)==I9100_GPIO_OWNER);haptic_parent_close(c);
}
static void pulse_and_timer()
{
 auto *c=haptic_parent();Supply s;i9100_haptic_worker *w=0;auto p=profile();unsigned before=pwm_barriers;
 assert(i9100_haptic_worker_create(0,c,&p,&w)==I9100_VIB_DISABLED&&!w&&before==pwm_barriers);
 assert(!i9100_haptic_worker_create(1,c,&p,&w));auto status=get_status(w);
 assert(status.ready&&status.parent_held&&status.power_held&&status.pin_owned&&status.clock_owned&&!status.active);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 assert(!i9100_haptic_worker_pulse(w,50,10));status=get_status(w);
 assert(status.active&&status.pwm_running&&status.pulses==1&&s.hardware.gpio.hapticMemory[2]==0xc3);
 assert((uint64_t)status.duty_ns*2U>=status.period_ns&&(uint64_t)status.duty_ns*100U<=(uint64_t)status.period_ns*55U);
 assert(i9100_haptic_worker_pulse(w,20,10)==I9100_VIB_BUSY);
 std::this_thread::sleep_for(std::chrono::milliseconds(60));pwm_loop->tick();status=get_status(w);
 assert(!status.active&&!status.pwm_running&&status.timer_events==1&&!status.fault&&s.hardware.gpio.memory[0x48]==0x28);
 assert(!i9100_haptic_worker_pulse(w,50,10)&&!i9100_haptic_worker_stop(w));
 assert(i9100_haptic_worker_pulse(w,251,10)==I9100_VIB_ARGUMENT);
 assert(i9100_haptic_worker_status(w,(struct i9100_haptic_worker_status *)w)==I9100_VIB_ARGUMENT);
 assert(!i9100_haptic_worker_destroy(&w)&&!w);
 assert(pwm_memory[0x139d0000U][0x18/4]==77&&pwm_memory[0x139d0000U][0x1c/4]==33&&pwm_memory[0x139d0000U][8/4]==1);
 s.close();haptic_parent_close(c);
}
static void foreign_hardware()
{
 for(unsigned kind=0;kind<3;kind++){
  auto *c=haptic_parent();Supply s;i9100_haptic_worker *w=0;auto p=profile();
  if(kind==0)pwm_memory[0x10490000U][0x108/4]=1U<<6;
  if(kind==1)pwm_memory[0x139d0000U][8/4]|=0x100U;
  if(kind==2)pwm_memory[0x11400000U][0xa0/4]|=1U<<4;
  uint32_t gpio=pwm_memory[0x11400000U][0xa0/4],control=pwm_memory[0x139d0000U][8/4];
  assert(i9100_haptic_worker_create(1,c,&p,&w)==I9100_PWM_BUSY&&!w);
  assert(pwm_memory[0x11400000U][0xa0/4]==gpio&&pwm_memory[0x139d0000U][8/4]==control);
  if(kind==0)pwm_memory[0x10490000U][0x108/4]=0;
  if(kind==1)pwm_memory[0x139d0000U][8/4]&=~0x100U;
  if(kind==2)pwm_memory[0x11400000U][0xa0/4]&=~(15U<<4);
  s.close();haptic_parent_close(c);
 }
}
static void failure_retention()
{
 for(unsigned kind=0;kind<4;kind++){
  auto *c=haptic_parent();Supply s;i9100_haptic_worker *w=0;auto p=profile();assert(!i9100_haptic_worker_create(1,c,&p,&w));
  if(kind==0){pwm_timer_fail=1;assert(i9100_haptic_worker_pulse(w,50,10)==I9100_VIB_IO);assert(get_status(w).fault&&!get_status(w).active);pwm_timer_fail=0;}
  if(kind==1){assert(!i9100_haptic_worker_pulse(w,50,10));pwm_stop_drop=1;
   assert(i9100_haptic_worker_destroy(&w)==I9100_VIB_IO&&w&&get_status(w).parent_held&&get_status(w).pin_owned);pwm_stop_drop=0;}
  if(kind==2){assert(!i9100_haptic_worker_pulse(w,50,10)&&!i9100_haptic_worker_stop(w));pwm_restore_drop=1;
   assert(i9100_haptic_worker_destroy(&w)==I9100_PWM_IO&&w&&get_status(w).pwm_prepared);pwm_restore_drop=0;}
  if(kind==3){pwm_gate_drop=1;assert(i9100_haptic_worker_destroy(&w)==I9100_CLOCK_IO&&w&&get_status(w).parent_held&&get_status(w).clock_owned);pwm_gate_drop=0;}
  assert(!i9100_haptic_worker_destroy(&w)&&!w);s.close();haptic_parent_close(c);
 }
}
static void allocation_and_remove()
{
 for(unsigned point=1;point<=7;point++){
  auto *c=haptic_parent();Supply s;i9100_haptic_worker *w=0;auto p=profile();unsigned before=haptic_live;
  haptic_fail_allocation=haptic_allocations+point;int e=i9100_haptic_worker_create(1,c,&p,&w);
  assert(e&&!w&&haptic_live==before);haptic_fail_allocation=0;s.close();haptic_parent_close(c);
 }
 auto *c=haptic_parent();Supply s;i9100_haptic_worker *w=0;auto p=profile();pwm_add_fail=1;
 assert(i9100_haptic_worker_create(1,c,&p,&w)==-99&&!w);pwm_add_fail=0;
 assert(!i9100_haptic_worker_create(1,c,&p,&w));pwm_remove_fail=1;
 assert(i9100_haptic_worker_destroy(&w)==-98&&w&&get_status(w).closed&&!get_status(w).parent_held);
 assert(!i9100_peripheral_native_destroy(&c)&&!c);pwm_loop->tick();pwm_remove_fail=0;
 assert(!i9100_haptic_worker_destroy(&w));s.close();assert(haptic_live==3U);
}
static void revoked_provider()
{
 auto *c=haptic_parent();Supply s;i9100_haptic_worker *w=0;auto p=profile();
 assert(!i9100_haptic_worker_create(1,c,&p,&w)&&!i9100_haptic_worker_pulse(w,50,10));
 assert(!i9100_power_shared_unpublish(s.owner));s.owner->release();assert(!s.dead);
 pwm_loop->tick();auto state=get_status(w);
 assert(state.fault&&!state.active&&!state.pwm_running&&state.error==I9100_SHARED_NOT_READY);
 assert(s.hardware.gpio.hapticMemory[2]==0x83&&s.hardware.gpio.memory[0x48]==0x28);
 assert(i9100_haptic_worker_pulse(w,50,10)==I9100_VIB_DISABLED);
 assert(!i9100_haptic_worker_destroy(&w)&&s.dead);haptic_parent_close(c);
}
int main()
{
 assert(!i9100_power_shared_prepare(1));parent_reference();pulse_and_timer();foreign_hardware();failure_retention();allocation_and_remove();revoked_provider();
 puts("PASS real haptic workloop/native parent/GPIO/clock/Power/PWM/vibrator: low duty/timer stop/repeated pulses/foreign GPIO+GIC exclusion/7 allocations/failed PWM stop+mode+gate restore/retained parent/remove after closed; MMIO/Power wire/IOKit are stand-ins, no motor or waveform proof");return 0;
}
