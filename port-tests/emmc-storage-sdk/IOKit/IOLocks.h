#include "../emmc_model.h"
struct IOSimpleLock {bool held;};typedef unsigned IOInterruptState;
IOSimpleLock *IOSimpleLockAlloc();void IOSimpleLockFree(IOSimpleLock *);
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *);
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *,IOInterruptState);
