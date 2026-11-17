/* SPDX-License-Identifier: BSD-2-Clause; real native parent plus MMIO/IOKit stand-ins. */
#ifndef I9100_HAPTIC_NATIVE_MODEL_H
#define I9100_HAPTIC_NATIVE_MODEL_H
#include "i9100_power_haptic_model.h"
#include <map>
#include <vector>
#include <typeinfo>
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_peripheral_native.cpp"
static std::map<uint32_t,std::vector<uint32_t> > pwm_memory;
static unsigned pwm_barriers,pwm_stop_drop,pwm_restore_drop,pwm_gate_drop,pwm_gate_seen;
static unsigned pwm_add_calls,pwm_add_fail,pwm_remove_fail,pwm_timer_fail;
static IOWorkLoop *pwm_loop;
static HapticHardware *pwm_power;
static bool pwm_programmed;
static unsigned pwm_last_mux;
IOSimpleLock *IOSimpleLockAlloc(){auto *p=(IOSimpleLock *)i9100_model_event_allocate(sizeof(IOSimpleLock));if(p)p->held=false;return p;}
void IOSimpleLockFree(IOSimpleLock *p){assert(!p->held);i9100_model_event_free(p);}
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *p){assert(!p->held);p->held=true;unsigned old=haptic_irq_enabled;haptic_irq_enabled=false;return old;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p,IOInterruptState old){assert(p->held);p->held=false;haptic_irq_enabled=old!=0;}
extern "C" int ml_set_interrupts_enabled(int n){int old=haptic_irq_enabled;haptic_irq_enabled=n!=0;return old;}
void OSSynchronizeIO()
{
 pwm_barriers++;
 if(pwm_memory.count(0x10030000U)){
  auto &cmu=pwm_memory[0x10030000U];if(cmu[0xc950/4]&(1U<<24))pwm_gate_seen=1;
  if(pwm_gate_drop&&pwm_gate_seen)cmu[0xc950/4]|=1U<<24;
 }
 if(pwm_memory.count(0x139d0000U)){
  auto &p=pwm_memory[0x139d0000U];
  if((p[8/4]&0xe00U)==0xc00U)pwm_programmed=true;
  if(pwm_stop_drop)p[8/4]|=0x100U;
  if(pwm_restore_drop&&pwm_programmed&&!(p[8/4]&0xf00U))p[8/4]|=0xc00U;
  if(pwm_programmed&&!(p[8/4]&0xf00U)&&pwm_memory.count(0x11400000U))
   assert(((pwm_memory[0x11400000U][0xa0/4]>>4)&15U)==0); /* input before restoring mode */
 }
 if(pwm_memory.count(0x11400000U)&&pwm_memory.count(0x139d0000U)){
  unsigned mux=(pwm_memory[0x11400000U][0xa0/4]>>4)&15U;
  if(mux!=pwm_last_mux&&mux==2U&&pwm_programmed){assert((pwm_memory[0x139d0000U][8/4]&0x300U)==0);}
  pwm_last_mux=mux;
 }
}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t physical,size_t bytes,unsigned)
{
 auto *p=(IOMemoryDescriptor *)i9100_model_event_allocate(sizeof(IOMemoryDescriptor));if(!p)return nullptr;
 p->physical=physical;p->bytes=bytes;if(!pwm_memory.count(physical))pwm_memory[physical].resize(bytes/4U);
 assert(pwm_memory[physical].size()*4U>=bytes);return p;
}
IOMemoryMap *IOMemoryDescriptor::map(unsigned)
{auto *p=(IOMemoryMap *)i9100_model_event_allocate(sizeof(IOMemoryMap));if(p)p->address=(uintptr_t)pwm_memory[physical].data();return p;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){i9100_model_event_free(this);}
void IOMemoryDescriptor::release(){i9100_model_event_free(this);}
IOWorkLoop *IOWorkLoop::workLoop(){pwm_loop=new IOWorkLoop;return pwm_loop;}
IOReturn IOWorkLoop::addEventSource(IOEventSource *p){if(++pwm_add_calls==pwm_add_fail)return -99;assert(count<4);p->retain();p->setWorkLoop(this);sources[count++]=p;return 0;}
IOReturn IOWorkLoop::removeEventSource(IOEventSource *p)
{if(pwm_remove_fail)return -98;unsigned n=0;while(n<count&&sources[n]!=p)n++;assert(n<count);p->setWorkLoop(nullptr);p->release();for(unsigned k=n;k+1<count;k++)sources[k]=sources[k+1];sources[--count]=nullptr;return 0;}
IOReturn IOWorkLoop::runAction(Action action,OSObject *owner,void *a,void *b,void *c,void *d)
{assert(haptic_irq_enabled&&owner&&typeid(*owner)!=typeid(OSObject));bool old=gate;gate=true;int e=action(owner,a,b,c,d);gate=old;return e;}
void IOEventSource::signalWorkAvailable(){assert(workLoop);workLoop->pending=true;}
void IOWorkLoop::pump(){assert(!gate);thread=gate=true;for(unsigned n=0;n<count;n++)sources[n]->checkForWork();thread=gate=false;}
void IOWorkLoop::tick(){assert(!gate);thread=gate=true;for(unsigned n=0;n<count;n++)if(auto *t=dynamic_cast<IOTimerEventSource *>(sources[n]))t->fire();thread=gate=false;}
void IOWorkLoop::free(){assert(!count&&!gate);}
IOReturn IOTimerEventSource::setTimeoutUS(UInt32 n){assert(n&&n<=250000U);if(pwm_timer_fail)return -97;queued=true;return 0;}
static i9100_peripheral_native *haptic_parent()
{
 assert(haptic_live==3U);pwm_memory.clear();pwm_barriers=pwm_stop_drop=pwm_restore_drop=pwm_gate_drop=pwm_gate_seen=0;
 pwm_add_calls=pwm_add_fail=pwm_remove_fail=pwm_timer_fail=0;pwm_loop=nullptr;pwm_programmed=false;pwm_last_mux=0;haptic_fail_allocation=0;
 i9100_peripheral_native *c=0;assert(!i9100_peripheral_native_create(1,&c));
 auto &p=pwm_memory[0x139d0000U];p.resize(0x1000/4);p[0]=9;p[4/4]=0x33333533;p[8/4]=1;p[0x18/4]=77;p[0x1c/4]=33;
 pwm_memory[0x11400000U][0xa0/4]=0xa0000000U;pwm_memory[0x11400000U][0xa8/4]=0x11220000;pwm_memory[0x11400000U][0xac/4]=0x33440000;
 return c;
}
static void haptic_parent_close(i9100_peripheral_native *c)
{
 assert(pwm_memory[0x11400000U][0xa0/4]==0xa0000000U&&pwm_memory[0x11400000U][0xa8/4]==0x11220000&&pwm_memory[0x11400000U][0xac/4]==0x33440000);
 assert(!i9100_peripheral_native_destroy(&c)&&!c);assert(haptic_live==3U);
}
#endif
