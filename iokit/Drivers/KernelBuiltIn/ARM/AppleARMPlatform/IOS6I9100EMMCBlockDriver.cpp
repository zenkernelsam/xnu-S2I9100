/* SPDX-License-Identifier: BSD-2-Clause */
#include "IOS6I9100EMMCBlockDriver.h"
#include "IOS6I9100EMMCRaw.h"
#include <libkern/c++/OSString.h>
#include <machine/machine_routines.h>
OSDefineMetaClassAndStructors(IOS6I9100EMMCMedia,IOMedia)
OSDefineMetaClassAndStructors(IOS6I9100EMMCBlockDriver,IOBlockStorageDriver)

bool IOS6I9100EMMCMedia::matchPropertyTable(OSDictionary *table,SInt32 *score)
{
 if(!table||!IOMedia::matchPropertyTable(table,score))return false;
 OSObject *requested=table->getObject("IOClass");
 if(!requested)return true; /* Existing-service / resource notifications. */
 OSString *driver=OSDynamicCast(OSString,requested);if(!driver)return false;
 if(driver->isEqualTo("IOMediaBSDClient"))return true;
 return isWhole()&&!isWritable()&&driver->isEqualTo("IOS6I9100EMMCStorage");
}
IOService *IOS6I9100EMMCBlockDriver::probe(IOService *provider,SInt32 *score)
{
 IOS6I9100EMMCRaw *raw=OSDynamicCast(IOS6I9100EMMCRaw,provider);
 if(!raw||!raw->nativeContext()||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return 0;
 struct i9100_emmc_native_snapshot current;
 if(i9100_emmc_native_validate(raw->nativeContext())||i9100_emmc_native_snapshot(raw->nativeContext(),&current)||
    current.mode!=I9100_EMMC_N_DATA_RW||current.error||current.sealed||
    !current.system_hfs_valid||!current.data_hfs_valid)return 0;
 return IOBlockStorageDriver::probe(provider,score);
}
IOMedia *IOS6I9100EMMCBlockDriver::instantiateDesiredMediaObject()
{return new IOS6I9100EMMCMedia;}
