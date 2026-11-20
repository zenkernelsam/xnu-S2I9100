/* SPDX-License-Identifier: BSD-2-Clause */
#include "IOS6I9100EMMCRaw.h"
#include <IOKit/storage/IOStorage.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSString.h>
#include <machine/machine_routines.h>
OSDefineMetaClassAndStructors(IOS6I9100EMMCRaw,IOBlockStorageDevice)
bool IOS6I9100EMMCRaw::init(OSDictionary *p)
{native=0;held=false;lock=0;worker=0;sectors=0;stopping=false;return IOBlockStorageDevice::init(p);}
bool IOS6I9100EMMCRaw::bind(int enabled,i9100_emmc_native *n)
{
 if(enabled!=1||!n||native||lock||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return false;
 struct i9100_emmc_native_snapshot s;
 if(i9100_emmc_native_snapshot(n,&s)||s.error||s.identity.sectors!=30777344ULL)return false;
 lock=IOLockAlloc();if(!lock||i9100_emmc_native_hold_get(n,(uintptr_t)this))return false;
 native=n;held=true;sectors=s.identity.sectors;return i9100_emmc_worker_create(1,n,&worker)==0;
}
bool IOS6I9100EMMCRaw::start(IOService *p)
{return held&&native&&worker&&!stopping&&IOBlockStorageDevice::start(p)&&setProperty("device-type","Generic")&&setProperty("I9100NativeEMMCWholeReadOnly",true);}
bool IOS6I9100EMMCRaw::matchPropertyTable(OSDictionary *table,SInt32 *score)
{
 if(!table||!IOBlockStorageDevice::matchPropertyTable(table,score))return false;
 OSObject *requested=table->getObject("IOClass");if(!requested)return true;
 OSString *driver=OSDynamicCast(OSString,requested);
 return driver&&driver->isEqualTo("IOS6I9100EMMCBlockDriver");
}
void IOS6I9100EMMCRaw::free()
{
 if(worker){int e=i9100_emmc_worker_destroy(&worker);if(e)IOLog("i9100 native raw: queue retained e=%d\n",e);}
 if(held&&native){int e=i9100_emmc_native_hold_put(native,(uintptr_t)this);if(e)IOLog("i9100 native raw: retain failed consumer e=%d\n",e);}
 if(lock)IOLockFree(lock);native=0;held=false;IOBlockStorageDevice::free();
}
IOReturn IOS6I9100EMMCRaw::workValidate(OSObject *owner,UInt64 token,unsigned operation)
{auto *s=(IOS6I9100EMMCRaw *)owner;IOLockLock(s->lock);IOReturn e=!s->held||!s->native||s->stopping?kIOReturnNoMedia:
 token!=1U||operation!=I9100_EMMC_WORK_READ?kIOReturnNotWritable:kIOReturnSuccess;IOLockUnlock(s->lock);return e;}
i9100_emmc_native *IOS6I9100EMMCRaw::nativeContext() const
{if(!lock||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return 0;IOLockLock(lock);
 i9100_emmc_native *n=held&&worker&&!stopping?native:0;IOLockUnlock(lock);return n;}
bool IOS6I9100EMMCRaw::terminate(IOOptionBits options)
{if(lock){IOLockLock(lock);stopping=true;IOLockUnlock(lock);}if(worker)i9100_emmc_worker_cancel(worker,this,1U);
 return IOBlockStorageDevice::terminate(options);}
void IOS6I9100EMMCRaw::stop(IOService *provider)
{if(lock){IOLockLock(lock);stopping=true;IOLockUnlock(lock);}if(worker){IOReturn e=i9100_emmc_worker_stop(worker);
 if(e){IOLog("i9100 native raw: retain queue during stop e=%x\n",e);return;}}IOBlockStorageDevice::stop(provider);}
IOReturn IOS6I9100EMMCRaw::doAsyncReadWrite(IOMemoryDescriptor *buffer,UInt64 block,UInt64 count,
 IOStorageAttributes *attrs,IOStorageCompletion *completion)
{
 if(!buffer||!completion||!completion->action||!held||!count||count>I9100_EMMC_MAX_SECTORS||
    block>=sectors||count>sectors-block||buffer->getLength()!=count*512U)return kIOReturnBadArgument;
 if(buffer->getDirection()!=kIODirectionIn)return kIOReturnNotWritable;
 if(ml_at_interrupt_context()||!ml_get_interrupts_enabled())return kIOReturnNotPermitted;
 if(attrs&&(attrs->options||attrs->reserved0032||attrs->reserved0064))return kIOReturnUnsupported;
 return i9100_emmc_worker_submit(worker,this,1U,workValidate,I9100_EMMC_WORK_READ,block,count,buffer,completion);
}
char *IOS6I9100EMMCRaw::getVendorString(){static char s[]="Samsung";return s;}
char *IOS6I9100EMMCRaw::getProductString(){static char s[]="GT-I9100 native eMMC RO whole";return s;}
char *IOS6I9100EMMCRaw::getRevisionString(){static char s[]="MSHCI32 queued";return s;}
char *IOS6I9100EMMCRaw::getAdditionalDeviceInfoString(){static char s[]="DATAFS writes require verified partition provider";return s;}
IOReturn IOS6I9100EMMCRaw::reportBlockSize(UInt64 *p){if(!p)return kIOReturnBadArgument;*p=512U;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportMaxValidBlock(UInt64 *p){if(!p)return kIOReturnBadArgument;*p=sectors-1U;return held?kIOReturnSuccess:kIOReturnNoMedia;}
IOReturn IOS6I9100EMMCRaw::reportMaxReadTransfer(UInt64,UInt64 *p){if(!p)return kIOReturnBadArgument;*p=512U*I9100_EMMC_MAX_SECTORS;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportMaxWriteTransfer(UInt64,UInt64 *p){if(!p)return kIOReturnBadArgument;*p=0;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportEjectability(bool *p){if(!p)return kIOReturnBadArgument;*p=false;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportLockability(bool *p){return reportEjectability(p);}
IOReturn IOS6I9100EMMCRaw::reportRemovability(bool *p){return reportEjectability(p);}
IOReturn IOS6I9100EMMCRaw::reportMediaState(bool *p,bool *changed){if(!p||!changed)return kIOReturnBadArgument;*p=nativeContext()!=0;*changed=false;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportPollRequirements(bool *p,bool *expensive){if(!p||!expensive)return kIOReturnBadArgument;*p=*expensive=false;return kIOReturnSuccess;}
IOReturn IOS6I9100EMMCRaw::reportWriteProtection(bool *p){if(!p)return kIOReturnBadArgument;*p=true;return kIOReturnSuccess;}
