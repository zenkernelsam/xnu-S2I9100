/* SPDX-License-Identifier: BSD-2-Clause; actual edge-level wire and owner model. */
#ifndef I9100_POWER_HAPTIC_MODEL_H
#define I9100_POWER_HAPTIC_MODEL_H
#include "i9100_power_shared.h"
#include "offline-native-sdk/IOKit/i9100_model_events.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <stdlib.h>
#define main i9100_embedded_power_tests_main
#include "i9100_power_test.cpp"
#undef main
static std::atomic<unsigned> haptic_allocations(0),haptic_live(0);
static thread_local bool haptic_irq_enabled=true,haptic_in_irq=false;
static thread_local char haptic_thread_tag;
void *i9100_model_event_allocate(size_t n){haptic_allocations++;haptic_live++;return calloc(1,n);}
void i9100_model_event_free(void *p){assert(p&&haptic_live);haptic_live--;free(p);}
void *IOMalloc(size_t n){return i9100_model_event_allocate(n);}
void IOFree(void *p,size_t){if(p)i9100_model_event_free(p);}
IOLock *IOLockAlloc(){auto *p=(IOLock *)i9100_model_event_allocate(sizeof(IOLock));p->held=false;return p;}
void IOLockFree(IOLock *p){assert(!p->held);i9100_model_event_free(p);}
bool IOLockTryLock(IOLock *p){return __sync_bool_compare_and_swap(&p->held,false,true);}
void IOLockUnlock(IOLock *p){assert(p->held);__sync_lock_release(&p->held);}
extern "C" int ml_get_interrupts_enabled(){return haptic_irq_enabled;}
extern "C" int ml_at_interrupt_context(){return haptic_in_irq;}
extern "C" void *current_thread(){return &haptic_thread_tag;}
extern "C" uint64_t mach_absolute_time(){return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
extern "C" void absolutetime_to_nanoseconds(uint64_t n,uint64_t *out){*out=n;}
void IODelay(unsigned us){std::this_thread::sleep_for(std::chrono::microseconds(us));}
struct HapticOwner : OSObject {bool *dead;explicit HapticOwner(bool *p):dead(p){}void free() override{*dead=true;}};
struct HapticHardware {
 PowerGPIO gpio;std::atomic<bool> pause,entered;bool fail_after_config;
 explicit HapticHardware(bool rail_on=false):gpio(false,true),pause(false),entered(false),fail_after_config(false){
  gpio.memory[0]=0x97;gpio.memory[1]=3;gpio.memory[0x48]=rail_on?0xe8:0x28;gpio.hapticMemory[2]=0x83;
 }
 static uint32_t read(void *c,unsigned r,unsigned off){return PowerGPIO::read(&((HapticHardware *)c)->gpio,r,off);}
 static void write(void *c,unsigned r,unsigned off,uint32_t v){PowerGPIO::write(&((HapticHardware *)c)->gpio,r,off,v);}
 static void delay(void *c,unsigned us){PowerGPIO::delay(&((HapticHardware *)c)->gpio,us);}
 static void enter(void *c){auto *a=(HapticHardware *)c;PowerGPIO::enter(&a->gpio);a->entered=true;while(a->pause.load())std::this_thread::yield();}
 static void leave(void *c){PowerGPIO::leave(&((HapticHardware *)c)->gpio);}
 static int read_config(void *hw,uint8_t *out){return i9100_power_haptic_config_read(hw,out);}
 static int write_config(void *hw,uint8_t value){auto *p=(i9100_power_hw *)hw;auto *a=(HapticHardware *)p->cookie;
  int ok=i9100_power_haptic_config_write(hw,value);return a->fail_after_config&&(value&0x40U)?0:ok;}
 i9100_power_hw io(){i9100_power_hw hw={this,read,write,delay,enter,leave};return hw;}
 i9100_power_haptic_ops ops(){i9100_power_haptic_ops ops={read_config,write_config,1};return ops;}
};
static void wait_entered(HapticHardware &hardware)
{
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(!hardware.entered.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
 assert(hardware.entered.load());
}
#endif
