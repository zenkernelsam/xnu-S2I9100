/* Actual shared gate + production RTC wire with explicit GPIO/owner facades. */
#include "i9100_power_haptic_model.h"
#include <unistd.h>
struct ChargeRTC : HapticHardware {
 explicit ChargeRTC(bool rail_on=false):HapticHardware(rail_on){
  gpio.withRTC=true;const uint8_t time[7]={30,1,13,2,11,26,20};
  gpio.rtcMemory[3]=2;memcpy(gpio.rtcMemory+0x10,time,sizeof(time));
 }
 uint64_t time_value(){uint64_t value=0;assert(i9100_rtc_decode(gpio.rtcMemory+0x10,2,&value));return value;}
};
static void concurrent_motor_and_time()
{
 ChargeRTC power;auto hw=power.io();auto ops=power.ops();bool dead=false;
 auto *owner=new HapticOwner(&dead);assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));
 i9100_power_haptic_client motor={};assert(!i9100_power_haptic_open(&motor,1));
 assert(!i9100_power_haptic_rail(&motor,1));
 uint8_t before[256];memcpy(before,power.gpio.rtcMemory,sizeof(before));
 const uint64_t expected=power.time_value();std::atomic<unsigned> reads(0),writes(0);
 std::thread rtc([&]{for(unsigned n=0;n<16;n++){
  uint64_t current=0;assert(i9100_power_rtc_seconds(&hw,&current)&&current==expected);reads++;
 }});
 std::thread haptic([&]{for(unsigned n=0;n<16;n++){
  assert(!i9100_power_haptic_write(&motor,0xc3));
  assert(!i9100_power_haptic_write(&motor,0x83));writes+=2;
 }});
 rtc.join();haptic.join();
 assert(reads==16U&&writes==32U&&!motor.fault&&!motor.motor_owned);
 assert(!memcmp(before,power.gpio.rtcMemory,sizeof(before)));
 assert(power.gpio.hapticMemory[2]==0x83&&power.gpio.memory[0x48]==0xe8);
 assert(power.gpio.enters==power.gpio.leaves&&!power.gpio.guard);
 assert(!i9100_power_haptic_rail(&motor,0)&&!i9100_power_haptic_close(&motor));
 assert(!i9100_power_shared_unpublish(owner));owner->release();assert(dead);
}
static void contention_and_no_resend()
{
 ChargeRTC power;auto hw=power.io();uint64_t value=0xabcdef0123ULL;
 i9100_power_bus_ticket hold={};assert(i9100_power_bus_enter(0,&hold));
 unsigned entries=power.gpio.enters;
 auto began=std::chrono::steady_clock::now();
 std::thread rtc([&]{assert(!i9100_power_rtc_seconds(&hw,&value));});rtc.join();
 assert(std::chrono::steady_clock::now()-began<std::chrono::seconds(1));
 assert(entries==power.gpio.enters&&value==0xabcdef0123ULL&&i9100_power_bus_leave(&hold));
 haptic_irq_enabled=false;assert(!i9100_power_rtc_seconds(&hw,&value)&&entries==power.gpio.enters);haptic_irq_enabled=true;
 haptic_in_irq=true;assert(!i9100_power_rtc_seconds(&hw,&value)&&entries==power.gpio.enters);haptic_in_irq=false;
 assert(i9100_power_rtc_seconds(&hw,&value)&&value==power.time_value());
 // A fresh later read cannot clear an already inherited policy timeout.
 BatteryModel chip;auto io=chip.io();i9100_power_state state;assert(!i9100_power_init(&state,&io,14));
 auto handoff=handoff_record(14,20,14019);chip.time=22000000000ULL;
 assert(i9100_charge_adopt(&state,&handoff,sizeof(handoff),chip.time)==I9100_CHARGE_ADOPT_APPLIED);
 uint64_t inherited=state.charge_elapsed_base;assert(!i9100_power_poll(&state)&&state.enabled);
 chip.time=6000000000000ULL;assert(!i9100_power_poll(&state)&&state.timeout&&!state.enabled);
 assert(i9100_power_rtc_seconds(&hw,&value)&&value==power.time_value());
 assert(!i9100_power_poll(&state)&&state.timeout&&!state.enabled&&state.charge_elapsed_base==inherited);
}
static void resource_filter()
{
 i9100_power_bus_ticket ticket={};assert(i9100_power_bus_enter(0,&ticket));uint8_t bytes[7]={};
 assert(i9100_power_bus_filter(&ticket,0x06,0x03,bytes,1,0));
 assert(i9100_power_bus_filter(&ticket,0x06,0x10,bytes,7,0));
 for(unsigned reg: {0x03U,0x04U,0x05U,0x06U,0x10U,0x17U})
  assert(!i9100_power_bus_filter(&ticket,0x06,reg,bytes,1,1));
 assert(!i9100_power_bus_filter(&ticket,0x06,0x10,bytes,6,0));
 assert(!i9100_power_bus_filter(&ticket,0x06,0x11,bytes,7,0));
 assert(i9100_power_bus_leave(&ticket));
 assert(i9100_power_bus_enter(1,&ticket));assert(!i9100_power_bus_filter(&ticket,0x06,0x10,bytes,7,0));
 assert(i9100_power_bus_leave(&ticket));
}
int main()
{
 alarm(20);assert(!i9100_power_shared_prepare(1));
 concurrent_motor_and_time();contention_and_no_resend();resource_filter();
 assert(haptic_live==3U);
 puts("PASS actual shared RTC/haptic/Power: concurrent readonly time and authorized motor writes, retained resources, bounded IRQ/contention refusal, alarms/update writes denied and inherited deadline preserved; hardware modeled");
}
