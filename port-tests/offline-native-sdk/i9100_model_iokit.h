/* SPDX-License-Identifier: BSD-2-Clause
 * Host-only IOKit facade for the native audio resource-lifecycle model.
 * These declarations are never included by the ARM/XNU compiler. */
#ifndef I9100_MODEL_IOKIT_H
#define I9100_MODEL_IOKIT_H
#include <stddef.h>
#include <stdint.h>
enum {kIODirectionInOut=3,kIOMapInhibitCache=0x100};
struct IOLock {bool held;};
struct IOSimpleLock {bool held;};
typedef unsigned IOInterruptState;
IOSimpleLock *IOSimpleLockAlloc();void IOSimpleLockFree(IOSimpleLock *);
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *);
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *,IOInterruptState);
void *IOMalloc(size_t);void IOFree(void *,size_t);
IOLock *IOLockAlloc();void IOLockFree(IOLock *);bool IOLockTryLock(IOLock *);void IOLockUnlock(IOLock *);
void OSSynchronizeIO();void IODelay(unsigned);
class IOMemoryMap {
public:
 uintptr_t address;
 uintptr_t getAddress();void release();
};
class IOMemoryDescriptor {
public:
 uint32_t physical;size_t bytes;
 static IOMemoryDescriptor *withPhysicalAddress(uint32_t,size_t,unsigned);
 IOMemoryMap *map(unsigned);void release();
};
#endif
