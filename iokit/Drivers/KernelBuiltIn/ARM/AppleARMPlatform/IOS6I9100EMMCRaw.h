/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef IOS6_I9100_EMMC_RAW_H
#define IOS6_I9100_EMMC_RAW_H
#include <IOKit/storage/IOBlockStorageDevice.h>
#include "i9100_emmc_native.h"
class IOS6I9100EMMCRaw : public IOBlockStorageDevice {
 OSDeclareDefaultStructors(IOS6I9100EMMCRaw)
 i9100_emmc_native *native;bool held;
 IOLock *lock;uint8_t *bounce;uint64_t sectors;
public:
 bool init(OSDictionary *p=0) override;
 bool bind(int enabled,i9100_emmc_native *);
 bool start(IOService *) override;
 void free() override;
 i9100_emmc_native *nativeContext() const{return held?native:0;}
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
/* The ordinary IOBlockStorageDriver creates a true RO whole IOMedia.
 * The partition service attaches to it; DATAFS writes then use the exact
 * native consumer path with lower scope checks. Every raw write/format/
 * cache-write/eject/erase is rejected; this wrapper never probes again.
 * Explicit platform create/bind/start only, no auto-activation. */
#endif
