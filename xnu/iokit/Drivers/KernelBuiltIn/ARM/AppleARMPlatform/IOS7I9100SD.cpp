/* I9100 Exynos4210 SDHCI block storage device (read-only PIO).
 * Exposes the SD card as IOBlockStorageDevice so IOS7LeoHFSFile can
 * locate IOS7/ROOTFS.HFS via fat_locator. Hardware PAL is a direct
 * port of the uniLoader sdhci4210.c sequence proven on QEMU smdkc210. */
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOStorage.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOCatalogue.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSUnserialize.h>
#include <kern/debug.h>
#include "i9100_sdhci.h"
#include "IOS7LabStoragePersonalities.h"
extern "C" {
#include <mach/mach_time.h>
addr64_t kvtophys(vm_offset_t va);
}
class IOS7I9100SD : public IOBlockStorageDevice {
 OSDeclareDefaultStructors(IOS7I9100SD)
 IOMemoryDescriptor *mmio; IOMemoryMap *map; volatile UInt8 *regs;
 IOLock *mutex; struct i9100_sd_card card; bool initialized;
 static uint32_t readMMIO32(void *,uint32_t,unsigned);
 static uint16_t readMMIO16(void *,uint32_t,unsigned);
 static void writeMMIO32(void *,uint32_t,unsigned,uint32_t);
 static void writeMMIO16(void *,uint32_t,unsigned,uint16_t);
 static void writeMMIO8(void *,uint32_t,unsigned,uint8_t);
 static void delayUS(void *,uint32_t);
public:
 virtual bool init(OSDictionary *properties=0);
 virtual bool start(IOService *provider);
 virtual void free();
 virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *,UInt64,UInt64,IOStorageAttributes *,IOStorageCompletion *);
 virtual IOReturn doEjectMedia(){return kIOReturnUnsupported;}
 virtual IOReturn doFormatMedia(UInt64){return kIOReturnUnsupported;}
 virtual UInt32 doGetFormatCapacities(UInt64 *,UInt32)const{return 0;}
 virtual IOReturn doLockUnlockMedia(bool){return kIOReturnUnsupported;}
 virtual IOReturn doSynchronizeCache(){return kIOReturnUnsupported;}
 virtual char *getVendorString();virtual char *getProductString();
 virtual char *getRevisionString();virtual char *getAdditionalDeviceInfoString();
 virtual IOReturn reportBlockSize(UInt64 *);
 virtual IOReturn reportMaxValidBlock(UInt64 *);
 virtual IOReturn reportEjectability(bool *);
 virtual IOReturn reportLockability(bool *);
 virtual IOReturn reportRemovability(bool *);
 virtual IOReturn reportMediaState(bool *,bool *);
 virtual IOReturn reportPollRequirements(bool *,bool *);
 virtual IOReturn reportWriteProtection(bool *);
};
OSDefineMetaClassAndStructors(IOS7I9100SD,IOBlockStorageDevice)
static int i9100sd_iolog=-1;
static bool sdIologOn(void)
{
 if(i9100sd_iolog<0){int v=0;
  extern int PE_parse_boot_argn(const char *,void *,int);
  PE_parse_boot_argn("i9100iolog",&v,sizeof(v));i9100sd_iolog=v?1:0;}
 return i9100sd_iolog>0;
}
/* regs[] covers the whole 0x40000 four-host span; callbacks get the
 * host PA and index off it. */
uint32_t IOS7I9100SD::readMMIO32(void *o,uint32_t pa,unsigned r)
{ IOS7I9100SD *d=(IOS7I9100SD*)o; OSSynchronizeIO();
  return *(volatile UInt32*)(d->regs+(pa-0x12510000U)+r); }
uint16_t IOS7I9100SD::readMMIO16(void *o,uint32_t pa,unsigned r)
{ IOS7I9100SD *d=(IOS7I9100SD*)o; OSSynchronizeIO();
  return *(volatile UInt16*)(d->regs+(pa-0x12510000U)+r); }
void IOS7I9100SD::writeMMIO32(void *o,uint32_t pa,unsigned r,uint32_t v)
{ IOS7I9100SD *d=(IOS7I9100SD*)o; *(volatile UInt32*)(d->regs+(pa-0x12510000U)+r)=v; OSSynchronizeIO(); }
void IOS7I9100SD::writeMMIO16(void *o,uint32_t pa,unsigned r,uint16_t v)
{ IOS7I9100SD *d=(IOS7I9100SD*)o; *(volatile UInt16*)(d->regs+(pa-0x12510000U)+r)=v; OSSynchronizeIO(); }
void IOS7I9100SD::writeMMIO8(void *o,uint32_t pa,unsigned r,uint8_t v)
{ IOS7I9100SD *d=(IOS7I9100SD*)o; *(volatile UInt8*)(d->regs+(pa-0x12510000U)+r)=v; OSSynchronizeIO(); }
void IOS7I9100SD::delayUS(void *,uint32_t us){ IODelay(us); }
bool IOS7I9100SD::init(OSDictionary *p)
{
 mmio=0; map=0; regs=0; mutex=0; initialized=false;
 struct i9100_sd_io io={this,readMMIO32,readMMIO16,writeMMIO32,writeMMIO16,writeMMIO8,delayUS};
 card.io=io; card.host_pa=0; card.sectors=0; card.sdhc=0; card.ready=0;
 return IOBlockStorageDevice::init(p);
}
bool IOS7I9100SD::start(IOService *provider)
{
 if(!IOBlockStorageDevice::start(provider)) return false;
 mutex=IOLockAlloc(); if(!mutex) return false;
 mmio=IOMemoryDescriptor::withPhysicalAddress(0x12510000U,0x40000,kIODirectionInOut);
 if(!mmio) return false;
 map=mmio->map(kIOMapInhibitCache);
 if(!map||!map->getAddress()) return false;
 regs=(volatile UInt8*)map->getAddress();
 int rc=i9100_sd_probe(&card);
 if(rc!=I9100_SD_OK){
  IOLog("I9100 SDHCI probe failed rc=%d\n",rc); return false;
 }
 initialized=true;
 IOLog("I9100 SDHCI: host@%08x sectors=%llu sdhc=%d\n",
       (unsigned)card.host_pa,(unsigned long long)card.sectors,card.sdhc);
 /* Publish personalities so partition scheme + HFS file attach. */
 OSObject *dec=OSUnserializeXML(ios7labStoragePersonalities);
 OSArray *drv=OSDynamicCast(OSArray,dec);
 if(!drv||!gIOCatalogue->addDrivers(drv,false)) { if(dec)dec->release(); return false; }
 dec->release();
 return true;
}
void IOS7I9100SD::free(){ if(mutex)IOLockFree(mutex); if(map)map->release(); if(mmio)mmio->release(); IOBlockStorageDevice::free(); }
char *IOS7I9100SD::getVendorString(){static char t[]="Samsung";return t;}
char *IOS7I9100SD::getProductString(){static char t[]="I9100 SDHCI SD";return t;}
char *IOS7I9100SD::getRevisionString(){static char t[]="PIO";return t;}
char *IOS7I9100SD::getAdditionalDeviceInfoString(){static char t[]="I9100 SDHCI";return t;}
IOReturn IOS7I9100SD::reportBlockSize(UInt64 *v){if(!v)return kIOReturnBadArgument;*v=512;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::reportMaxValidBlock(UInt64 *v){if(!v||!initialized)return kIOReturnNotReady;*v=card.sectors-1;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::reportEjectability(bool *v){if(!v)return kIOReturnBadArgument;*v=false;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::reportLockability(bool *v){if(!v)return kIOReturnBadArgument;*v=false;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::reportRemovability(bool *v){if(!v)return kIOReturnBadArgument;*v=false;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::reportMediaState(bool *p,bool *c){if(!p||!c)return kIOReturnBadArgument;*p=initialized;*c=false;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::reportPollRequirements(bool *r,bool *e){if(!r||!e)return kIOReturnBadArgument;*r=false;*e=false;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::reportWriteProtection(bool *v){if(!v)return kIOReturnBadArgument;*v=true;return kIOReturnSuccess;}
IOReturn IOS7I9100SD::doAsyncReadWrite(IOMemoryDescriptor *buffer,UInt64 block,UInt64 count,
 IOStorageAttributes *attributes,IOStorageCompletion *completion)
{
 UInt64 bytes=0;
 if(!buffer||!completion||!completion->action||!initialized||
    block>=card.sectors||count==0||block+count>card.sectors||
    count*512U!=buffer->getLength())return kIOReturnBadArgument;
 if(buffer->getDirection()!=kIODirectionIn)return kIOReturnNotWritable;
 if(attributes&&(attributes->options||attributes->reserved0032||attributes->reserved0064))return kIOReturnUnsupported;
 bytes=count*512U;
 IOStorageCompletion saved=*completion;UInt64 completed=0;IOReturn result=kIOReturnSuccess;
 uint8_t *tmp=(uint8_t*)IOMalloc((unsigned)(count*512U));
 if(!tmp)return kIOReturnNoMemory;
 {
  /* CORRUPTION PROBE: log tmp VA and the physical frame its PTE actually
   * targets; PIO writes land wherever this points. */
  addr64_t tp=kvtophys((vm_offset_t)(uintptr_t)tmp);
  addr64_t te=(bytes>1)?kvtophys((vm_offset_t)(uintptr_t)(tmp+bytes-1)):tp;
  unsigned tv=(unsigned)(uintptr_t)tmp;
  if((tv>=0x7ffff000U&&tv<0xc0000000U)||
     (tp<0x80000000ULL&&tp>=0x5d000000ULL)||
     (te<0x80000000ULL&&te>=0x5d000000ULL))
   if(sdIologOn())
   IOLog("I9100SDTMP blk=%llu n=%llu tmp=%p kvp0=%llx kvpN=%llx\n",
         (unsigned long long)block,(unsigned long long)count,
         tmp,(unsigned long long)tp,(unsigned long long)te);
 }
 IOLockLock(mutex);
 {
  int rc=i9100_sd_read_multi(&card,block,tmp,(uint32_t)count);
  if(rc!=I9100_SD_OK)result=kIOReturnIOError;
  else if(buffer->writeBytes(0,tmp,(unsigned)bytes)!=(unsigned)bytes)result=kIOReturnUnderrun;
  else completed=bytes;
 }
 IOLockUnlock(mutex);
 IOFree(tmp,(unsigned)bytes);
 {
  static unsigned long i9100_sdrw_n;
  if(((++i9100_sdrw_n) & 0xfffUL)==1UL && sdIologOn())
   IOLog("I9100SDRW blk=%llu n=%llu result=%d done=%llu rd#%lu\n",
         (unsigned long long)block,(unsigned long long)count,result,
         (unsigned long long)completed,i9100_sdrw_n);
 }
 IOStorage::complete(&saved,result,completed);return kIOReturnSuccess;
}
extern "C" void ios7i9100_register_sd(IOService *platform)
{
 IOS7I9100SD *device=new IOS7I9100SD;if(!device){panic("I9100 SD object allocation failed");return;}
 bool attached=false,success=false;
 if(device->init())attached=device->attach(platform);
 if(attached&&device->start(platform)){
  device->registerService();success=true;
 }
 if(!success)IOLog("I9100 SD: object init/attach/map/start failed\n");
 if(attached&&!success)device->detach(platform);
 device->release();
}
#endif
