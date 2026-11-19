/* SPDX-License-Identifier: BSD-2-Clause */
#include "IOS6I9100EMMCRaw.h"
#include <IOKit/storage/IOStorage.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <machine/machine_routines.h>
OSDefineMetaClassAndStructors(IOS6I9100EMMCRaw,IOBlockStorageDevice)
bool IOS6I9100EMMCRaw::init(OSDictionary *p)
{native=0;held=false;lock=0;bounce=0;sectors=0;return IOBlockStorageDevice::init(p);}
bool IOS6I9100EMMCRaw::bind(int enabled,i9100_emmc_native *n)
{
 if(enabled!=1||!n||native||lock||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return false;
 struct i9100_emmc_native_snapshot s;
 if(i9100_emmc_native_snapshot(n,&s)||s.error||s.identity.sectors!=30777344ULL)return false;
 lock=IOLockAlloc();bounce=(uint8_t *)IOMalloc(512U*I9100_EMMC_MAX_SECTORS);
 if(!lock||!bounce||i9100_emmc_native_hold_get(n,(uintptr_t)this))return false;
 native=n;held=true;sectors=s.identity.sectors;return true;
}
bool IOS6I9100EMMCRaw::start(IOService *p)
{return held&&native&&IOBlockStorageDevice::start(p)&&setProperty("device-type","Generic")&&setProperty("I9100NativeEMMCWholeReadOnly",true);}
void IOS6I9100EMMCRaw::free()
{
 if(held&&native){int e=i9100_emmc_native_hold_put(native,(uintptr_t)this);if(e)IOLog("i9100 native raw: retain failed consumer e=%d\n",e);}
 if(bounce)IOFree(bounce,512U*I9100_EMMC_MAX_SECTORS);if(lock)IOLockFree(lock);native=0;held=false;IOBlockStorageDevice::free();
}
IOReturn IOS6I9100EMMCRaw::doAsyncReadWrite(IOMemoryDescriptor *buffer,UInt64 block,UInt64 count,
 IOStorageAttributes *attrs,IOStorageCompletion *completion)
{
 if(!buffer||!completion||!completion->action||!held||!count||count>I9100_EMMC_MAX_SECTORS||
    block>=sectors||count>sectors-block||buffer->getLength()!=count*512U)return kIOReturnBadArgument;
 if(buffer->getDirection()!=kIODirectionIn)return kIOReturnNotWritable;
 if(ml_at_interrupt_context()||!ml_get_interrupts_enabled())return kIOReturnNotPermitted;
 if(attrs&&(attrs->options||attrs->reserved0032||attrs->reserved0064))return kIOReturnUnsupported;
 IOStorageCompletion done=*completion;UInt64 actual=0;retain();buffer->retain();IOLockLock(lock);
 int e=i9100_emmc_native_read(native,block,(uint32_t)count,bounce);IOReturn result=e?kIOReturnIOError:kIOReturnSuccess;
 if(!e){actual=buffer->writeBytes(0,bounce,count*512U);if(actual!=count*512U)result=kIOReturnUnderrun;}
 IOLockUnlock(lock);buffer->release();IOStorage::complete(&done,result,actual);release();return kIOReturnSuccess;
}
char *IOS6I9100EMMCRaw::getVendorString(){static char s[]="Samsung";return s;}
char *IOS6I9100EMMCRaw::getProductString(){static char s[]="GT-I9100 native eMMC RO whole";return s;}
char *IOS6I9100EMMCRaw::getRevisionString(){static char s[]="MSHCI PIO32";return s;}
char *IOS6I9100EMMCRaw::getAdditionalDeviceInfoString(){static char s[]="DATAFS writes require verified partition provider";return s;}
IOReturn IOS6I9100EMMCRaw::reportBlockSize(UInt64 *p){if(!p)return kIOReturnBadArgument;*p=512U;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportMaxValidBlock(UInt64 *p){if(!p)return kIOReturnBadArgument;*p=sectors-1U;return held?kIOReturnSuccess:kIOReturnNoMedia;}
IOReturn IOS6I9100EMMCRaw::reportMaxReadTransfer(UInt64,UInt64 *p){if(!p)return kIOReturnBadArgument;*p=512U*I9100_EMMC_MAX_SECTORS;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportMaxWriteTransfer(UInt64,UInt64 *p){if(!p)return kIOReturnBadArgument;*p=0;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportEjectability(bool *p){if(!p)return kIOReturnBadArgument;*p=false;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportLockability(bool *p){return reportEjectability(p);}
IOReturn IOS6I9100EMMCRaw::reportRemovability(bool *p){return reportEjectability(p);}
IOReturn IOS6I9100EMMCRaw::reportMediaState(bool *p,bool *changed){if(!p||!changed)return kIOReturnBadArgument;*p=held;*changed=false;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportPollRequirements(bool *p,bool *expensive){if(!p||!expensive)return kIOReturnBadArgument;*p=*expensive=false;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportWriteProtection(bool *p){if(!p)return kIOReturnBadArgument;*p=true;return kIOReturnSuccess;}
