/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef IOS6_I9100_EMMC_RAW_H
#define IOS6_I9100_EMMC_RAW_H
#include <IOKit/storage/IOBlockStorageDevice.h>
#include "i9100_emmc_native.h"
#include "i9100_emmc_worker.h"
class IOS6I9100EMMCRaw : public IOBlockStorageDevice {
 OSDeclareDefaultStructors(IOS6I9100EMMCRaw)
 i9100_emmc_native *native;bool held;
 IOLock *lock;i9100_emmc_worker *worker;uint64_t sectors;bool stopping;
 static IOReturn workValidate(OSObject *,UInt64,unsigned);
public:
 bool init(OSDictionary *p=0) override;
 bool bind(int enabled,i9100_emmc_native *);
 bool matchPropertyTable(OSDictionary *,SInt32 *) override;
 bool start(IOService *) override;
 bool terminate(IOOptionBits) override;
 void stop(IOService *) override;
 void free() override;
 i9100_emmc_native *nativeContext() const;
 IOReturn doAsyncReadWrite(IOMemoryDescriptor *,UInt64,UInt64,IOStorageAttributes *,IOStorageCompletion *) override;
 IOReturn doEjectMedia() override{return kIOReturnUnsupported;}
 IOReturn doFormatMedia(UInt64) override{return kIOReturnNotWritable;}
 UInt32 doGetFormatCapacities(UInt64 *,UInt32) const override{return 0;}
 IOReturn doLockUnlockMedia(bool) override{return kIOReturnUnsupported;}
 IOReturn doSynchronizeCache() override{return kIOReturnNotWritable;}
 char *getVendorString() override;char *getProductString() override;
 char *getRevisionString() override;char *getAdditionalDeviceInfoString() override;
 IOReturn reportBlockSize(UInt64 *) override;
 IOReturn reportMaxValidBlock(UInt64 *) override;
 IOReturn reportMaxReadTransfer(UInt64,UInt64 *) override;
 IOReturn reportMaxWriteTransfer(UInt64,UInt64 *) override;
 IOReturn reportEjectability(bool *) override;IOReturn reportLockability(bool *) override;
 IOReturn reportRemovability(bool *) override;
 IOReturn reportMediaState(bool *,bool *) override;
 IOReturn reportPollRequirements(bool *,bool *) override;
 IOReturn reportWriteProtection(bool *) override;
};
/* IOS6I9100EMMCBlockDriver uses the ordinary block driver mechanics to
 * create a true RO whole IOS6I9100EMMCMedia. Generic driver personalities
 * cannot attach to this native raw device.
 * The partition service attaches to it; DATAFS writes then use the exact
 * native consumer path with lower scope checks. Every raw write/format/
 * cache-write/eject/erase is rejected; this wrapper never probes again.
 * Explicit platform create/bind/start only, no auto-activation. */
#endif
