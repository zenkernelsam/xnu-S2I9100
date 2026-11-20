/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef IOS6_I9100_EMMC_BLOCK_DRIVER_H
#define IOS6_I9100_EMMC_BLOCK_DRIVER_H
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOMedia.h>

/* These nubs keep the inherited generic storage family from discovering
 * unreviewed partitions on the native disk. Notification queries without
 * IOClass still work; driver personalities are restricted by family match. */
class IOS6I9100EMMCMedia : public IOMedia {
 OSDeclareDefaultStructors(IOS6I9100EMMCMedia)
public:
 bool matchPropertyTable(OSDictionary *,SInt32 *) override;
};
class IOS6I9100EMMCBlockDriver : public IOBlockStorageDriver {
 OSDeclareDefaultStructors(IOS6I9100EMMCBlockDriver)
protected:
 IOMedia *instantiateDesiredMediaObject() override;
public:
 IOService *probe(IOService *,SInt32 *) override;
};
#endif
