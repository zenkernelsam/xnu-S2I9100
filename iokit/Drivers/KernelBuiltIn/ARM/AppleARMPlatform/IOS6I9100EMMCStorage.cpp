/* SPDX-License-Identifier: BSD-2-Clause */
#include "IOS6I9100EMMCStorage.h"
#include "IOS6I9100EMMCRaw.h"
#include "IOS6I9100EMMCBlockDriver.h"
#include "i9100_emmc_alias.h"
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSData.h>
#include <machine/machine_routines.h>
#include <string.h>
OSDefineMetaClassAndStructors(IOS6I9100EMMCStorage,IOStorage)
static IOReturn storage_error(int e)
{
 return !e?kIOReturnSuccess:e==I9100_EMMC_ARGUMENT?kIOReturnBadArgument:
  e==I9100_EMMC_PARTITION?kIOReturnNotWritable:e==I9100_EMMC_BUSY?kIOReturnBusy:
  e==I9100_EMMC_NO_DEVICE?kIOReturnNoMedia:e==I9100_EMMC_UNSUPPORTED?kIOReturnUnsupported:kIOReturnIOError;
}
bool IOS6I9100EMMCStorage::init(OSDictionary *p)
{
 native=0;media[0]=media[1]=0;opened[0]=opened[1]=0;access[0]=access[1]=0;
 mutex=0;bounce=0;held=stopping=published=sealed=false;bzero(&snapshot,sizeof(snapshot));
 return IOStorage::init(p);
}
bool IOS6I9100EMMCStorage::bind(int enabled,i9100_emmc_native *n)
{
 if(enabled!=1||!n||native||mutex||!ml_get_interrupts_enabled()||ml_at_interrupt_context())return false;
 struct i9100_emmc_native_snapshot observed;
 if(i9100_emmc_native_snapshot(n,&observed)||observed.mode!=I9100_EMMC_N_DATA_RW||observed.error||observed.sealed||
    !observed.system_hfs_valid||!observed.data_hfs_valid||!(observed.data_hfs.attributes&0x2000U))return false;
 mutex=IOLockAlloc();bounce=(uint8_t *)IOMalloc(512U*I9100_EMMC_MAX_SECTORS);
 if(!mutex||!bounce)return false;
 if(i9100_emmc_native_hold_get(n,(uintptr_t)this))return false;
 native=n;held=true;snapshot=observed;return true;
}
IOService *IOS6I9100EMMCStorage::probe(IOService *provider,SInt32 *score)
{
 IOMedia *whole=OSDynamicCast(IOS6I9100EMMCMedia,provider);
 IOBlockStorageDriver *driver=whole?OSDynamicCast(IOS6I9100EMMCBlockDriver,whole->getProvider()):0;
 IOS6I9100EMMCRaw *raw=driver?OSDynamicCast(IOS6I9100EMMCRaw,driver->getProvider()):0;
 if(!whole||!whole->isWhole()||whole->isWritable()||whole->getSize()!=30777344ULL*512U||!raw||
    !raw->nativeContext()||!IOStorage::probe(provider,score))return 0;
 if(!native&&!bind(1,raw->nativeContext()))return 0;
 if(native!=raw->nativeContext())return 0;if(score)*score+=5000;return this;
}
int IOS6I9100EMMCStorage::child(const IOService *client) const
{return client==media[0]&&media[0]?0:client==media[1]&&media[1]?1:-1;}
bool IOS6I9100EMMCStorage::attachMedia(unsigned role)
{
 const i9100_emmc_partition *p=role?&snapshot.data:&snapshot.system;
 IOMedia *m=new IOS6I9100EMMCMedia;if(!m)return false;
 if(!m->init(p->start_lba*512ULL,p->sectors*512ULL,512,(IOMediaAttributeMask)0,false,role!=0,"Apple_HFS")){m->release();return false;}
 m->setName(role?"DATAFS":"FACTORYFS");m->setLocation(role?"10":"9");
 if(!m->setProperty("I9100EMMCVolumeRole",role?"data":"system")||
    !m->setProperty("I9100EMMCStableAlias",role?"i9100-data":"i9100-system")||
    !m->setProperty("I9100VerifiedCurrentMedia",true)||!m->setProperty("Partition ID",p->gpt_number,32))
  {m->release();return false;}
 OSData *guid=OSData::withBytes(p->unique_guid,16),*disk=OSData::withBytes(snapshot.identity.disk_guid,16),
  *cid=OSData::withBytes(snapshot.identity.cid,sizeof(snapshot.identity.cid));
 bool ok=guid&&disk&&cid&&m->setProperty("I9100GPTUniqueGUID",guid)&&m->setProperty("I9100GPTDiskGUID",disk)&&m->setProperty("I9100EMMCCID",cid);
 if(guid)guid->release();if(disk)disk->release();if(cid)cid->release();
 if(!ok||!m->attach(this)){m->release();return false;}media[role]=m;return true;
}
bool IOS6I9100EMMCStorage::start(IOService *provider)
{
 IOMedia *whole=OSDynamicCast(IOS6I9100EMMCMedia,provider);
 IOBlockStorageDriver *driver=whole?OSDynamicCast(IOS6I9100EMMCBlockDriver,whole->getProvider()):0;
 IOS6I9100EMMCRaw *raw=driver?OSDynamicCast(IOS6I9100EMMCRaw,driver->getProvider()):0;
 if(!whole||!whole->isWhole()||whole->isWritable()||whole->getSize()!=30777344ULL*512U||!raw||raw->nativeContext()!=native)return false;
 if(!native||!held||stopping||published||!i9100_emmc_alias_prepare()||!IOStorage::start(provider))return false;
 if(!attachMedia(0)||!attachMedia(1)){
  for(unsigned i=0;i<2;i++)if(media[i]){media[i]->detach(this);media[i]->release();media[i]=0;}
  return false;
 }
 media[0]->registerService();media[1]->registerService();published=true;return true;
}
bool IOS6I9100EMMCStorage::handleOpen(IOService *client,IOOptionBits options,void *requested)
{
 int index=child(client);uint32_t wanted=(uint32_t)(uintptr_t)requested;
 if(index<0||options||!held||!native||(wanted!=kIOStorageAccessReader&&wanted!=kIOStorageAccessReaderWriter)||
    (index==0&&wanted==kIOStorageAccessReaderWriter))return false;
 /* IOService serializes its open callbacks. Two exact media children are
  * the only upstream clients; their own IOMedia handles reader/writer sets. */
 IOLockLock(mutex);if(stopping||sealed){IOLockUnlock(mutex);return false;}
 opened[index]=client;access[index]=wanted;IOLockUnlock(mutex);return true;
}
bool IOS6I9100EMMCStorage::handleIsOpen(const IOService *client) const
{int index=child(client);if(!mutex)return false;IOLockLock(mutex);
 bool result=client?(index>=0&&opened[index]==client):(opened[0]||opened[1]);IOLockUnlock(mutex);return result;}
void IOS6I9100EMMCStorage::handleClose(IOService *client,IOOptionBits)
{int index=child(client);if(index>=0){IOLockLock(mutex);if(opened[index]==client){opened[index]=0;access[index]=0;}IOLockUnlock(mutex);}}
void IOS6I9100EMMCStorage::transfer(bool writing,IOService *client,UInt64 start,IOMemoryDescriptor *buffer,
 IOStorageAttributes *attributes,IOStorageCompletion *completion)
{
 if(!completion||!completion->action)return;
 retain();IOStorageCompletion done=*completion;IOReturn result=kIOReturnSuccess;UInt64 actual=0;
 int index=child(client);
 if(index<0||!buffer||!native||!held||!ml_get_interrupts_enabled()||ml_at_interrupt_context())result=kIOReturnNotPermitted;
 else if(writing&&index!=1)result=kIOReturnNotWritable;
 else if(attributes&&(attributes->options||attributes->reserved0032||attributes->reserved0064))result=kIOReturnUnsupported;
 else{
  const i9100_emmc_partition *p=index?&snapshot.data:&snapshot.system;
  UInt64 base=p->start_lba*512ULL,capacity=p->sectors*512ULL,size=buffer->getLength();
  if(!size||(size&511U)||(start&511U)||start<base||start-base>=capacity||size>capacity-(start-base)||size>1024U*1024U)
   result=kIOReturnBadArgument;
  else{
   buffer->retain();IOLockLock(mutex);
   if(stopping||(sealed&&index==1)||!opened[index]||(writing&&access[index]!=kIOStorageAccessReaderWriter))result=kIOReturnNotOpen;
   while(actual<size&&result==kIOReturnSuccess){
    UInt64 n=size-actual;if(n>512U*I9100_EMMC_MAX_SECTORS)n=512U*I9100_EMMC_MAX_SECTORS;
    int e;
    if(writing){
     if(buffer->readBytes(actual,bounce,n)!=n){result=kIOReturnUnderrun;break;}
     e=i9100_emmc_native_write_data(native,(start+actual)/512U,(uint32_t)(n/512U),bounce);
    }else e=i9100_emmc_native_read(native,(start+actual)/512U,(uint32_t)(n/512U),bounce);
    if(e){result=storage_error(e);break;}
    if(!writing&&buffer->writeBytes(actual,bounce,n)!=n){result=kIOReturnUnderrun;break;}
    actual+=n;
   }
   IOLockUnlock(mutex);buffer->release();
  }
 }IOStorage::complete(&done,result,actual);release(); /* Caller callback never under card/service lock. */
}
void IOS6I9100EMMCStorage::read(IOService *client,UInt64 start,IOMemoryDescriptor *buffer,
 IOStorageAttributes *attrs,IOStorageCompletion *completion){transfer(false,client,start,buffer,attrs,completion);}
void IOS6I9100EMMCStorage::write(IOService *client,UInt64 start,IOMemoryDescriptor *buffer,
 IOStorageAttributes *attrs,IOStorageCompletion *completion){transfer(true,client,start,buffer,attrs,completion);}
IOReturn IOS6I9100EMMCStorage::synchronizeCache(IOService *client)
{
 int index=child(client);if(index!=1)return kIOReturnNotWritable;
 if(!native||!held||!ml_get_interrupts_enabled()||ml_at_interrupt_context())return kIOReturnNotOpen;
 IOLockLock(mutex);
 if(stopping||sealed||!opened[index]||access[index]!=kIOStorageAccessReaderWriter){IOLockUnlock(mutex);return kIOReturnNotOpen;}
 int e=i9100_emmc_native_synchronize(native);
 IOLockUnlock(mutex);return storage_error(e);
}
IOReturn IOS6I9100EMMCStorage::discard(IOService *,UInt64,UInt64){return kIOReturnUnsupported;}
IOMedia *IOS6I9100EMMCStorage::copyMedia(unsigned role)
{
 if(role<1U||role>2U||!mutex||!ml_get_interrupts_enabled()||ml_at_interrupt_context())return 0;
 IOLockLock(mutex);IOMedia *result=stopping?0:media[role-1U];if(result)result->retain();IOLockUnlock(mutex);return result;
}
bool IOS6I9100EMMCStorage::validateMedia(IOMedia *m,unsigned role) const
{
 if(role<1U||role>2U||!m||!mutex||!ml_get_interrupts_enabled()||ml_at_interrupt_context())return false;
 IOLockLock(mutex);
 if(media[role-1U]!=m||!native||!held||stopping||sealed||m->isWhole()||m->isWritable()!=(role==2U)){
  IOLockUnlock(mutex);return false;
 }
 struct i9100_emmc_native_snapshot current;
 if(i9100_emmc_native_validate(native)||i9100_emmc_native_snapshot(native,&current)||current.error||current.writer_poisoned){
  IOLockUnlock(mutex);return false;
 }
 const i9100_emmc_partition *p=role==1U?&current.system:&current.data;
 bool valid=m->getBase()==p->start_lba*512ULL&&m->getSize()==p->sectors*512ULL;
 IOLockUnlock(mutex);return valid;
}
int IOS6I9100EMMCStorage::sealData()
{
 if(!mutex||!native||!held||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return I9100_EMMC_ARGUMENT;
 IOLockLock(mutex);
 /* The read-only SYSTEM root may stay open. Only DATAFS writers/readers
  * must be gone; they are the sole clients that could dirty the card. */
 int e=stopping||sealed?I9100_EMMC_DIRTY:opened[1]?I9100_EMMC_BUSY:i9100_emmc_native_seal_data(native);
 if(!e)sealed=true;IOLockUnlock(mutex);return e;
}
bool IOS6I9100EMMCStorage::terminate(IOOptionBits options)
{
 /* An attached child retains the native card regardless of a failed
  * termination. Readiness must disappear before the standard framework
  * starts stopping BSD clients; a late alias commit must not resurrect it. */
 if(mutex)IOLockLock(mutex);stopping=true;if(mutex)IOLockUnlock(mutex);
 for(unsigned i=0;i<2;i++)if(media[i])i9100_emmc_alias_invalidate(media[i]);
 return IOStorage::terminate(options);
}
void IOS6I9100EMMCStorage::stop(IOService *provider)
{
 if(mutex)IOLockLock(mutex);
 if(opened[0]||opened[1]){if(mutex)IOLockUnlock(mutex);IOLog("i9100 EMMC storage: open volume prevents orderly stop\n");return;}
 stopping=true;if(mutex)IOLockUnlock(mutex);
 /* Never take alias gate while storage lock is held. Invalidate readiness
  * before a child can detach or BSD makes its original nodes obsolete. */
 for(unsigned i=0;i<2;i++)if(media[i]&&media[i]->getProvider()==this){
  i9100_emmc_alias_invalidate(media[i]);
  /* terminate() returning true only starts the framework's asynchronous
   * teardown. Never detach a live BSD subtree ourselves. Its existing
   * attachment pins this service/card until normal stop/finalize completes.
   * If termination was already requested, let that request finish. */
  if(!media[i]->isInactive())(void)media[i]->terminate(kIOServiceRequired);
 }
 IOStorage::stop(provider);
}
void IOS6I9100EMMCStorage::free()
{
 if(held&&native){
  /* Each child's attachment retains this service. No final free occurs
   * while a child/IO is in flight; the native consumer stays pinned. */
  int e=i9100_emmc_native_hold_put(native,(uintptr_t)this);
  if(e)IOLog("i9100 EMMC storage: preserve failed native consumer e=%d\n",e);else held=false;
 }
 for(unsigned i=0;i<2;i++)if(media[i]){media[i]->release();media[i]=0;}
 if(bounce)IOFree(bounce,512U*I9100_EMMC_MAX_SECTORS);if(mutex)IOLockFree(mutex);native=0;IOStorage::free();
}
