/* Host declaration of the methods implemented by the actual RO anchor. */
#ifndef I9100_RAW_STORAGE_MODEL_H
#define I9100_RAW_STORAGE_MODEL_H
#include "IOStorage.h"
class IOBlockStorageDevice: public IOService {
public:
 virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *,UInt64,UInt64,IOStorageAttributes *,IOStorageCompletion *)=0;
 virtual IOReturn doEjectMedia()=0;virtual IOReturn doFormatMedia(UInt64)=0;
 virtual UInt32 doGetFormatCapacities(UInt64 *,UInt32)const=0;
 virtual IOReturn doLockUnlockMedia(bool)=0;virtual IOReturn doSynchronizeCache()=0;
 virtual char *getVendorString()=0;virtual char *getProductString()=0;
 virtual char *getRevisionString()=0;virtual char *getAdditionalDeviceInfoString()=0;
 virtual IOReturn reportBlockSize(UInt64 *)=0;virtual IOReturn reportMaxValidBlock(UInt64 *)=0;
 virtual IOReturn reportMaxReadTransfer(UInt64,UInt64 *)=0;virtual IOReturn reportMaxWriteTransfer(UInt64,UInt64 *)=0;
 virtual IOReturn reportEjectability(bool *)=0;virtual IOReturn reportLockability(bool *)=0;
 virtual IOReturn reportRemovability(bool *)=0;virtual IOReturn reportMediaState(bool *,bool *)=0;
 virtual IOReturn reportPollRequirements(bool *,bool *)=0;virtual IOReturn reportWriteProtection(bool *)=0;
};
#endif
