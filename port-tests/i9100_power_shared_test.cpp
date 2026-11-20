/* SPDX-License-Identifier: BSD-2-Clause; original electrical/policy model plus real shared gate. */
#include "i9100_power_shared.h"
#include "i9100_radio_survey.h"
#include "offline-native-sdk/IOKit/i9100_model_events.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <stdlib.h>
#define main i9100_original_power_tests_main
#include "i9100_power_test.cpp"
#undef main
static std::atomic<unsigned> allocations(0),live(0);
static unsigned fail_at;
static thread_local bool irq_enabled=true,in_irq=false;
static thread_local char thread_tag;
void *i9100_model_event_allocate(size_t n){unsigned seq=++allocations;if(seq==fail_at)return nullptr;live++;return calloc(1,n);}
void i9100_model_event_free(void *p){assert(p&&live);live--;free(p);}
void *IOMalloc(size_t n){return i9100_model_event_allocate(n);}
void IOFree(void *p,size_t){if(p)i9100_model_event_free(p);}
IOLock *IOLockAlloc(){auto *p=(IOLock *)i9100_model_event_allocate(sizeof(IOLock));if(p)p->held=false;return p;}
void IOLockFree(IOLock *p){assert(!p->held);i9100_model_event_free(p);}
bool IOLockTryLock(IOLock *p){return __sync_bool_compare_and_swap(&p->held,false,true);}
void IOLockUnlock(IOLock *p){assert(p->held);__sync_lock_release(&p->held);}
void IOLockLock(IOLock *p){while(!IOLockTryLock(p))std::this_thread::yield();}
extern "C" int ml_get_interrupts_enabled(){return irq_enabled;}
extern "C" int ml_at_interrupt_context(){return in_irq;}
extern "C" void *current_thread(){return &thread_tag;}
extern "C" uint64_t mach_absolute_time(){return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
extern "C" void absolutetime_to_nanoseconds(uint64_t n,uint64_t *out){*out=n;}
void IODelay(unsigned us){std::this_thread::sleep_for(std::chrono::microseconds(us));}
struct Owner : OSObject {bool *dead;explicit Owner(bool *p):dead(p){}void free() override{*dead=true;}};
static std::atomic<unsigned> concurrent(0),maximum(0);
struct Actor {
 PowerGPIO gpio;std::atomic<bool> block,entered;unsigned enters;
 Actor():gpio(false),block(false),entered(false),enters(0){gpio.memory[0x3b]=0x91;gpio.memory[0]=0x97;}
 static uint32_t read(void *c,unsigned r,unsigned off){return PowerGPIO::read(&((Actor *)c)->gpio,r,off);}
 static void write(void *c,unsigned r,unsigned off,uint32_t v){PowerGPIO::write(&((Actor *)c)->gpio,r,off,v);}
 static void delay(void *c,unsigned us){PowerGPIO::delay(&((Actor *)c)->gpio,us);std::this_thread::yield();}
 static void enter(void *c){auto *a=(Actor *)c;a->enters++;unsigned n=++concurrent;unsigned old=maximum.load();while(old<n&&!maximum.compare_exchange_weak(old,n)){}
  PowerGPIO::enter(&a->gpio);a->entered=true;while(a->block.load())std::this_thread::yield();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 static void leave(void *c){PowerGPIO::leave(&((Actor *)c)->gpio);concurrent--;}
 i9100_power_hw io(){i9100_power_hw hw={this,read,write,delay,enter,leave};return hw;}
};
static void parallel_cookies()
{
 Actor power,mali;auto phw=power.io(),mhw=mali.io();maximum=0;concurrent=0;
 std::thread first([&]{for(unsigned n=0;n<12U;n++){uint8_t v=0;assert(i9100_power_pmic_read(&phw,0x3b,&v)&&v==0x91);}});
 std::thread second([&]{for(unsigned n=0;n<12U;n++){uint8_t v=0;assert(i9100_power_pmic_read(&mhw,0x3b,&v)&&v==0x91);}});
 first.join();second.join();assert(maximum==1U&&!concurrent&&power.enters==12U&&mali.enters==12U);
}
static void tickets()
{
 i9100_power_bus_ticket t={};assert(i9100_power_bus_enter(0,&t));i9100_power_bus_ticket copy=t;
 uint8_t rtc[7]={};assert(i9100_power_bus_filter(&t,0x06,3,rtc,1,0));
 assert(i9100_power_bus_filter(&t,0x06,0x10,rtc,7,0));
 assert(!i9100_power_bus_filter(&t,0x06,3,rtc,1,1));
 assert(!i9100_power_bus_filter(&t,0x06,0x04,rtc,1,0));
 assert(!i9100_power_bus_filter(&t,0x06,0x10,rtc,6,0));
 assert(!i9100_power_bus_filter(&t,0x06,0x11,rtc,7,0));
 assert(!i9100_power_bus_filter(&copy,0x06,3,rtc,1,0));
 assert(!i9100_power_bus_leave(&copy));std::thread other([&]{assert(!i9100_power_bus_leave(&t));});other.join();
 i9100_power_bus_ticket nested={};assert(!i9100_power_bus_enter(0,&nested));assert(i9100_power_bus_leave(&t)&&!i9100_power_bus_leave(&t));
 in_irq=true;assert(!i9100_power_bus_enter(0,&t));in_irq=false;irq_enabled=false;assert(!i9100_power_bus_enter(0,&t));irq_enabled=true;
 assert(i9100_power_bus_enter(1,&t));assert(!i9100_power_bus_filter(&t,0x06,3,rtc,1,0));assert(i9100_power_bus_leave(&t));
}
static void rtc_enabled_wire()
{
 PowerGPIO rtc(false,false,true);auto hw=rtc.io();uint8_t when[7]={29,1,13,2,11,26,20};
 rtc.rtcMemory[3]=2;memcpy(rtc.rtcMemory+0x10,when,7);uint64_t actual=0,expected=0;
 assert(i9100_rtc_decode(when,2,&expected)&&i9100_power_rtc_seconds(&hw,&actual)&&actual==expected);
 assert(rtc.received==std::vector<uint8_t>({0x0c,3,0x0d,0x0c,0x10,0x0d,0x0c,0x10,0x0d,0x0c,3,0x0d}));
 assert(!memcmp(rtc.rtcMemory+0x10,when,7)&&rtc.enters==rtc.leaves&&!rtc.guard);
 uint64_t unchanged=0xaabb;rtc.nack=true;assert(!i9100_power_rtc_seconds(&hw,&unchanged)&&unchanged==0xaabb);
}
static void reader_lifetime()
{
 Actor actor;auto hw=actor.io();bool dead=false;Owner *owner=new Owner(&dead);
 assert(!i9100_power_shared_publish(owner,&hw));i9100_power_read_client c={};assert(!i9100_power_read_open(&c));
 actor.gpio.memory[1]=3;actor.gpio.memory[0x13]=1;actor.gpio.memory[0x4b]=0xe8;
 struct i9100_radio_survey survey;assert(!i9100_radio_survey_read(&c,&survey)&&survey.ap_clock_programmed&&survey.vtf_programmed_mv==2800U);
 assert(!survey.electrical_verified&&!survey.wiring_verified);
 uint8_t v=0;assert(!i9100_power_read_pmic(&c,0x3b,&v)&&v==0x91);unsigned before=actor.enters;
 assert(i9100_power_read_pmic(&c,2,&v)==I9100_SHARED_ARGUMENT&&actor.enters==before);
 assert(i9100_power_read_pmic(&c,3,&v)==I9100_SHARED_ARGUMENT&&actor.enters==before);
 i9100_power_read_client copy=c;assert(i9100_power_read_pmic(&copy,0,&v)==I9100_SHARED_ARGUMENT&&actor.enters==before);
 actor.gpio.nack=true;v=0xcc;assert(i9100_power_read_pmic(&c,0,&v)==I9100_SHARED_IO&&v==0xcc);actor.gpio.nack=false;
 actor.block=true;actor.entered=false;v=0xcc;int result=0;
 std::thread reading([&]{result=i9100_power_read_pmic(&c,0x3b,&v);});
 while(!actor.entered.load())std::this_thread::yield();assert(i9100_power_read_close(&c)==I9100_SHARED_BUSY);
 assert(!i9100_power_shared_unpublish(owner)&&!dead);i9100_power_read_client rejected={};assert(i9100_power_read_open(&rejected)==I9100_SHARED_NOT_READY);
 actor.block=false;reading.join();assert(result==I9100_SHARED_NOT_READY&&v==0xcc&&!dead);
 assert(!i9100_power_read_close(&c)&&!dead);assert(i9100_power_read_close(&c)==I9100_SHARED_ARGUMENT);owner->release();assert(dead);
}
static void client_limits_and_replacement()
{
 Actor actor;auto hw=actor.io();bool dead1=false,dead2=false;Owner *one=new Owner(&dead1),*two=new Owner(&dead2);
 assert(!i9100_power_shared_publish(one,&hw));i9100_power_read_client clients[33]={};
 for(unsigned n=0;n<32U;n++)assert(!i9100_power_read_open(clients+n));assert(i9100_power_read_open(clients+32)==I9100_SHARED_NOT_READY);
 assert(!i9100_power_shared_unpublish(one)&&!i9100_power_shared_publish(two,&hw));
 uint8_t v=0xee;assert(i9100_power_read_pmic(clients,0,&v)==I9100_SHARED_NOT_READY&&v==0xee);
 assert(!i9100_power_read_open(clients+32)&&!i9100_power_read_pmic(clients+32,0,&v)&&v==0x97);
 for(unsigned n=0;n<32U;n++)assert(!i9100_power_read_close(clients+n));one->release();assert(dead1&&!dead2);
 assert(!i9100_power_shared_unpublish(two)&&!i9100_power_read_close(clients+32));two->release();assert(dead2);
}
int main(int argc,char **argv)
{
 if(argc==2){fail_at=(unsigned)strtoul(argv[1],nullptr,10);assert(i9100_power_shared_prepare(1)==I9100_SHARED_IO&&!live);
  i9100_power_bus_ticket t={};assert(!i9100_power_bus_enter(0,&t));puts("PASS prepare allocation failure denies raw access without leaking locks");return 0;}
 assert(i9100_power_shared_prepare(0)==I9100_SHARED_DISABLED&&!allocations);
 assert(!i9100_original_power_tests_main());i9100_power_bus_ticket legacy={};assert(i9100_power_bus_enter(0,&legacy));
 assert(i9100_power_shared_prepare(1)==I9100_SHARED_BUSY&&i9100_power_bus_leave(&legacy));
 assert(!i9100_power_shared_prepare(1)&&i9100_power_shared_enabled());parallel_cookies();tickets();rtc_enabled_wire();reader_lifetime();client_limits_and_replacement();
 assert(live==3U); /* intentional boot-lifetime mutexes, no owner/context leaks */
 puts("PASS shared common raw gate across Power/Mali cookies/real GPIO wire/legacy policy/late-enable fence/thread tickets/IRQ reject/bounded contention/safe-register filtering/client-copy/IO faults/unpublish during read/owner lifetime/32 clients/replacement and revoked results; host hardware model only");return 0;
}
