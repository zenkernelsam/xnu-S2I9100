/* SPDX-License-Identifier: BSD-2-Clause
 * Opt-in read-only Exynos4210 eMMC device. Offline bring-up module:
 * caller must provide a verified effective CIU frequency, not a guess. */
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOStorage.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOCatalogue.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSUnserialize.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "i9100_emmc.h"
#include "i9100_emmc_host.h"
#include "IOS7LabStoragePersonalities.h"
#undef absolutetime_to_nanoseconds /* use the underlying uint64 kernel timebase function */
extern void ios7i9100_install_gpt_ro_personality(void);
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
volatile uint32_t i9100_emmc_diag[32]={1};
}
class IOS7I9100EMMC : public IOBlockStorageDevice {
 OSDeclareDefaultStructors(IOS7I9100EMMC)
 IOMemoryDescriptor *mmio;IOMemoryMap *mapping;volatile UInt8 *regs;
 IOLock *mutex;UInt8 *bounce;bool initialized,lastPresent;
 struct i9100_emmc_card card;
 struct i9100_emmc_host_lease *hostLease;
 static uint32_t read32(void *cookie,unsigned offset){
  IOS7I9100EMMC *s=(IOS7I9100EMMC *)cookie;
  OSSynchronizeIO();UInt32 value=*(volatile UInt32 *)(s->regs+offset);OSSynchronizeIO();return value;
 }
 static void write32(void *cookie,unsigned offset,uint32_t value){
  IOS7I9100EMMC *s=(IOS7I9100EMMC *)cookie;
  OSSynchronizeIO();*(volatile UInt32 *)(s->regs+offset)=value;OSSynchronizeIO();
 }
 static uint64_t now(void *){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
 static int pause(void *,uint32_t us){IODelay(us);return 0;}
 void publish(){
  i9100_emmc_diag[1]=card.stage;i9100_emmc_diag[2]=(uint32_t)card.last_error;
  i9100_emmc_diag[3]=card.ready;i9100_emmc_diag[4]=card.version;i9100_emmc_diag[5]=card.hcon;
  i9100_emmc_diag[6]=card.clock_hz;i9100_emmc_diag[7]=card.clock_div;
  i9100_emmc_diag[8]=(uint32_t)card.sectors;i9100_emmc_diag[9]=(uint32_t)(card.sectors>>32);
  i9100_emmc_diag[10]=card.last_command;i9100_emmc_diag[11]=card.last_rint;
  i9100_emmc_diag[12]=card.last_status;i9100_emmc_diag[13]=card.last_response;
  for(unsigned n=0;n<4;n++){i9100_emmc_diag[14+n]=card.cid[n];i9100_emmc_diag[18+n]=card.csd[n];}
  i9100_emmc_diag[22]=(uint32_t)card.completed_sectors;i9100_emmc_diag[23]=(uint32_t)(card.completed_sectors>>32);
  i9100_emmc_diag[24]=card.ext_csd[192];i9100_emmc_diag[25]=card.ext_csd[179];
  i9100_emmc_diag[26]=card.ext_csd[61];i9100_emmc_diag[27]=card.ext_csd[33];
 }
public:
 virtual bool init(OSDictionary *p=0){
  mmio=0;mapping=0;regs=0;mutex=0;bounce=0;hostLease=0;initialized=lastPresent=false;
  i9100_emmc_io io={this,read32,write32,now,pause,0,20000000U,50000U};
  i9100_emmc_init(&card,&io);return IOBlockStorageDevice::init(p);
 }
 virtual bool start(IOService *provider){
  int requested=0,clock=0;
  if(!PE_parse_boot_argn("i9100emmcro",&requested,sizeof(requested))||requested!=1||
     !PE_parse_boot_argn("i9100emmcclk",&clock,sizeof(clock))||clock<400000||clock>200000000)return false;
  if(ml_at_interrupt_context()||!ml_get_interrupts_enabled()||!IOBlockStorageDevice::start(provider))return false;
  hostLease=(i9100_emmc_host_lease *)IOMalloc(sizeof(*hostLease));if(!hostLease)return false;
  bzero(hostLease,sizeof(*hostLease));if(i9100_emmc_host_acquire(hostLease))return false;
  mutex=IOLockAlloc();bounce=(UInt8 *)IOMalloc(512U*I9100_EMMC_MAX_SECTORS);
  mmio=IOMemoryDescriptor::withPhysicalAddress(I9100_EMMC_PA,I9100_EMMC_MMIO_SIZE,kIODirectionInOut);
  if(!mutex||!bounce||!mmio)return false;
  mapping=mmio->map(kIOMapInhibitCache);if(!mapping||!mapping->getAddress())return false;
  regs=(volatile UInt8 *)mapping->getAddress();card.io.input_clock_hz=(UInt32)clock;
  int error=i9100_emmc_probe(&card);publish();
  IOLog("i9100 eMMC RO: stage=%u cmd=%u rc=%d rint=%08x status=%08x resp=%08x\n",
        card.stage,card.last_command,error,card.last_rint,card.last_status,card.last_response);
  if(error)return false;
  char cid[33];snprintf(cid,sizeof(cid),"%08x%08x%08x%08x",card.cid[0],card.cid[1],card.cid[2],card.cid[3]);
  setProperty("device-type","Generic");setProperty("I9100EMMCCID",cid);
  setProperty("I9100EMMCUserAreaOnly",true);setProperty("I9100EMMCReadOnly",true);
  setProperty("I9100EMMCInputClockHz",card.io.input_clock_hz,32);
  initialized=true;return true;
 }
 virtual void free(){
  if(hostLease){
   bool idle=!regs||!(read32(this,EM_CMD)&(1U<<31))&&
    !(read32(this,EM_STATUS)&((15U<<4)|(1U<<9)|(1U<<10)|(0x1fffU<<17)|(1U<<30)|(1U<<31)));
   if(!hostLease->owned||!i9100_emmc_host_release(hostLease,idle?1:0))IOFree(hostLease,sizeof(*hostLease));
   else IOLog("i9100 eMMC RO: preserve failed host lease; controller is not quiescent\n");
   /* A retained lease has standalone memory, never a freed IOService
    * address. It prevents a second driver from adopting a failed card. */
   hostLease=0;
  }
  if(bounce)IOFree(bounce,512U*I9100_EMMC_MAX_SECTORS);
  if(mutex)IOLockFree(mutex);if(mapping)mapping->release();if(mmio)mmio->release();
  IOBlockStorageDevice::free();
 }
 virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer,UInt64 block,UInt64 count,IOStorageAttributes *a,IOStorageCompletion *completion){
  if(!buffer||!completion||!completion->action||!initialized||!count||count>I9100_EMMC_MAX_SECTORS||
     block>=card.sectors||count>card.sectors-block||count*512U!=buffer->getLength())return kIOReturnBadArgument;
  if(buffer->getDirection()!=kIODirectionIn)return kIOReturnNotWritable;
  if(ml_at_interrupt_context()||!ml_get_interrupts_enabled())return kIOReturnNotPermitted;
  if(a&&(a->options||a->reserved0032||a->reserved0064))return kIOReturnUnsupported;
  IOStorageCompletion saved=*completion;UInt64 actual=0;
  IOLockLock(mutex);int error=i9100_emmc_read(&card,block,(uint32_t)count,bounce);
  IOReturn result=error?kIOReturnIOError:kIOReturnSuccess;
  if(!error){actual=buffer->writeBytes(0,bounce,count*512U);if(actual!=count*512U)result=kIOReturnUnderrun;}
  publish();IOLockUnlock(mutex);IOStorage::complete(&saved,result,actual);return kIOReturnSuccess;
 }
 virtual IOReturn doEjectMedia(){return kIOReturnUnsupported;}
 virtual IOReturn doFormatMedia(UInt64){return kIOReturnNotWritable;}
 virtual UInt32 doGetFormatCapacities(UInt64 *,UInt32)const{return 0;}
 virtual IOReturn doLockUnlockMedia(bool){return kIOReturnUnsupported;}
 virtual IOReturn doSynchronizeCache(){return kIOReturnUnsupported;}
 virtual char *getVendorString(){static char s[]="Samsung";return s;}
 virtual char *getProductString(){static char s[]="GT-I9100 internal eMMC RO";return s;}
 virtual char *getRevisionString(){static char s[]="MMC PIO32";return s;}
 virtual char *getAdditionalDeviceInfoString(){static char s[]="No persistent writes; no DMA";return s;}
 virtual IOReturn reportBlockSize(UInt64 *v){if(!v)return kIOReturnBadArgument;*v=512;return kIOReturnSuccess;}
 virtual IOReturn reportMaxValidBlock(UInt64 *v){if(!v)return kIOReturnBadArgument;if(!card.ready)return kIOReturnNoMedia;*v=card.sectors-1;return kIOReturnSuccess;}
 virtual IOReturn reportMaxReadTransfer(UInt64,UInt64 *v){if(!v)return kIOReturnBadArgument;*v=512U*I9100_EMMC_MAX_SECTORS;return kIOReturnSuccess;}
 virtual IOReturn reportMaxWriteTransfer(UInt64,UInt64 *v){if(!v)return kIOReturnBadArgument;*v=0;return kIOReturnSuccess;}
 virtual IOReturn reportEjectability(bool *v){if(!v)return kIOReturnBadArgument;*v=false;return kIOReturnSuccess;}
 virtual IOReturn reportLockability(bool *v){if(!v)return kIOReturnBadArgument;*v=false;return kIOReturnSuccess;}
 virtual IOReturn reportRemovability(bool *v){if(!v)return kIOReturnBadArgument;*v=false;return kIOReturnSuccess;}
 virtual IOReturn reportMediaState(bool *present,bool *changed){if(!present||!changed)return kIOReturnBadArgument;*present=initialized&&card.ready;*changed=*present!=lastPresent;lastPresent=*present;return kIOReturnSuccess;}
 virtual IOReturn reportPollRequirements(bool *poll,bool *expensive){if(!poll||!expensive)return kIOReturnBadArgument;*poll=false;*expensive=false;return kIOReturnSuccess;}
 virtual IOReturn reportWriteProtection(bool *v){if(!v)return kIOReturnBadArgument;*v=true;return kIOReturnSuccess;}
};
OSDefineMetaClassAndStructors(IOS7I9100EMMC,IOBlockStorageDevice)
void ios7i9100_register_emmc_ro(IOService *platform)
{
 int requested=0;if(!PE_parse_boot_argn("i9100emmcro",&requested,sizeof(requested))||requested!=1)return;
 IOS7I9100EMMC *device=new IOS7I9100EMMC;if(!device)return;
 bool attached=false,started=false;
 if(device->init())attached=device->attach(platform);
 if(attached&&device->start(platform)){
  OSArray *a=OSDynamicCast(OSArray,OSUnserializeXML(ios7labStoragePersonalities));
  if(a){gIOCatalogue->addDrivers(a);a->release();}
  ios7i9100_install_gpt_ro_personality();
  device->registerService();started=true;
 }
 if(attached&&!started)device->detach(platform);device->release();
}
#endif
