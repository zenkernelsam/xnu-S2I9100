/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef IOS6_I9100_EMMC_STORAGE_H
#define IOS6_I9100_EMMC_STORAGE_H
#ifdef __cplusplus
#include <IOKit/storage/IOStorage.h>
#include <IOKit/storage/IOMedia.h>
#include "i9100_emmc_native.h"
class IOS6I9100EMMCStorage : public IOStorage {
 OSDeclareDefaultStructors(IOS6I9100EMMCStorage)
 i9100_emmc_native *native;
 IOMedia *media[2];IOService *opened[2];uint32_t access[2];
 IOLock *mutex;uint8_t *bounce;
 struct i9100_emmc_native_snapshot snapshot;
 bool held,stopping,published,sealed;
 int child(const IOService *) const;
 bool attachMedia(unsigned);
 void transfer(bool,IOService *,UInt64,IOMemoryDescriptor *,IOStorageAttributes *,IOStorageCompletion *);
protected:
 bool handleOpen(IOService *,IOOptionBits,void *) override;
 bool handleIsOpen(const IOService *) const override;
 void handleClose(IOService *,IOOptionBits) override;
public:
 bool init(OSDictionary *properties=0) override;
 IOService *probe(IOService *,SInt32 *) override;
 bool bind(int enabled,i9100_emmc_native *);
 bool start(IOService *) override;
 void stop(IOService *) override;
 void free() override;
 void read(IOService *,UInt64,IOMemoryDescriptor *,IOStorageAttributes *,IOStorageCompletion *) override;
 void write(IOService *,UInt64,IOMemoryDescriptor *,IOStorageAttributes *,IOStorageCompletion *) override;
 IOReturn synchronizeCache(IOService *) override;
 IOReturn discard(IOService *,UInt64,UInt64) override;
 IOMedia *copyMedia(unsigned role); /* retained IOMedia1=system,2=data */
 bool validateMedia(IOMedia *,unsigned role) const;
 int sealData(); /* both exact media closed; final native sync + seal */
};
/* Explicit platform binding only; no personality or boot flag starts it.
 * Native card must be DATA_RW with actual approved HFS roles. Publish two
 * IOMedia: systemRO, DATAFSRW. Whole raw medium is not writable/published.
 * Only the two exact child addresses may open/read/write; request offsets
 * are already translated by IOMedia. Runtime system writes/TRIM forbidden.
 * Stop refuses while users still hold media; consumers keep native/maps
 * alive until all IO is quiet and the service is explicitly torn down. */
#endif
#endif
