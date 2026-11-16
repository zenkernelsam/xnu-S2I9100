/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <map>
#include <vector>
/* Compile the actual native implementation in this TU: private fields are
 * visible only to fault/generation injection, no production test hook added. */
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_peripheral_native.cpp"
static std::map<uint32_t,std::vector<uint32_t> > memory;
static unsigned allocations,active_allocations,fail_alloc,barriers,drop_sdio_release;
static uint64_t ns;
static bool interrupts=true,sdio_gate_seen,freeze_model_time;
static bool allocate(){allocations++;if(allocations==fail_alloc)return false;active_allocations++;return true;}
void *IOMalloc(size_t n){return allocate()?calloc(1,n):0;}
void IOFree(void *p,size_t){if(p){assert(active_allocations);active_allocations--;free(p);}}
IOLock *IOLockAlloc(){return allocate()?new IOLock{false}:0;}
void IOLockFree(IOLock *p){if(p){assert(!p->held&&active_allocations);active_allocations--;delete p;}}
bool IOLockTryLock(IOLock *p){assert(p);if(p->held)return false;p->held=true;return true;}
void IOLockUnlock(IOLock *p){assert(p&&p->held);p->held=false;}
IOSimpleLock *IOSimpleLockAlloc(){return allocate()?new IOSimpleLock{false}:0;}
void IOSimpleLockFree(IOSimpleLock *p){if(p){assert(!p->held&&active_allocations);active_allocations--;delete p;}}
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *p){assert(p&&!p->held);p->held=true;unsigned saved=interrupts;interrupts=false;return saved;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p,IOInterruptState saved){assert(p&&p->held);p->held=false;interrupts=saved;}
extern "C" int ml_get_interrupts_enabled(){return interrupts;}
extern "C" int ml_set_interrupts_enabled(int value){int saved=interrupts;interrupts=value!=0;return saved;}
extern "C" uint64_t mach_absolute_time(){return ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
void IODelay(unsigned us){if(!freeze_model_time)ns+=(uint64_t)us*1000U;}
void OSSynchronizeIO()
{
 barriers++;
 if(memory.count(0x12540000U)){uint8_t *sd=(uint8_t *)memory[0x12540000U].data();sd[47]=0;}
 if(memory.count(0x10030000U)){
  uint32_t *cmu=memory[0x10030000U].data();
  if(cmu[0xc940/4]&(1U<<8))sdio_gate_seen=true;
  if(drop_sdio_release&&sdio_gate_seen&&!(cmu[0xc940/4]&(1U<<8)))cmu[0xc940/4]|=1U<<8;
 }
}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t physical,size_t n,unsigned)
{
 if(!allocate())return 0;
 IOMemoryDescriptor *p=new IOMemoryDescriptor;p->physical=physical;p->bytes=n;
 if(!memory.count(physical))memory[physical].resize(n/4U);
 assert(memory[physical].size()*4U>=n);return p;
}
IOMemoryMap *IOMemoryDescriptor::map(unsigned)
{if(!allocate())return 0;IOMemoryMap *p=new IOMemoryMap;p->address=(uintptr_t)memory[physical].data();return p;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(active_allocations);active_allocations--;delete this;}
void IOMemoryDescriptor::release(){assert(active_allocations);active_allocations--;delete this;}
static void setup()
{
 assert(!active_allocations);memory.clear();allocations=fail_alloc=barriers=drop_sdio_release=0;sdio_gate_seen=freeze_model_time=false;ns=0;interrupts=true;
 memory[0x13800000U].resize(1024);memory[0x13800000U][16/4]=4U;memory[0x13800000U][56/4]=15U;
}
static i9100_peripheral_native *parent()
{i9100_peripheral_native *c=0;assert(!i9100_peripheral_native_create(1,&c)&&c);return c;}
