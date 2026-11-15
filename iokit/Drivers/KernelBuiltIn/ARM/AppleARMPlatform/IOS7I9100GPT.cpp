/* SPDX-License-Identifier: BSD-2-Clause
 * Only the opt-in i9100 eMMC provider is accepted. Whole media and all
 * child slices remain read-only; this class never formats or repairs. */
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <IOKit/storage/IOPartitionScheme.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOCatalogue.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSUnserialize.h>
#include "i9100_emmc_layout.h"
class IOS7I9100GPT : public IOPartitionScheme {
 OSDeclareDefaultStructors(IOS7I9100GPT)
 IOMedia *raw;IOBufferMemoryDescriptor *bounce;
 i9100_emmc_layout *layout;OSArray *slices;
 static int read512(void *cookie,uint64_t lba,uint8_t out[512]){
  IOS7I9100GPT *s=(IOS7I9100GPT *)cookie;
  if(lba>=s->raw->getSize()/512U)return -1;
  UInt64 actual=0;IOReturn e=s->raw->read(s,lba*512U,s->bounce,&actual);
  return e!=kIOReturnSuccess||actual!=512||s->bounce->readBytes(0,out,512)!=512?-1:0;
 }
public:
 virtual bool init(OSDictionary *p=0){raw=0;bounce=0;layout=0;slices=0;return IOPartitionScheme::init(p);}
 virtual IOService *probe(IOService *provider,SInt32 *score){
  IOMedia *media=OSDynamicCast(IOMedia,provider);
  IOBlockStorageDriver *driver=media?OSDynamicCast(IOBlockStorageDriver,media->getProvider()):0;
  IOService *device=driver?driver->getProvider():0;
  if(!media||!media->isWhole()||media->isWritable()||media->getPreferredBlockSize()!=512||
     !device||!device->metaCast("IOS7I9100EMMC")||!media->getSize()||(media->getSize()&511U))return 0;
  if(!IOPartitionScheme::probe(provider,score))return 0;
  raw=media;raw->retain();
  layout=(i9100_emmc_layout *)IOMalloc(sizeof(*layout));
  bounce=IOBufferMemoryDescriptor::withOptions(kIODirectionIn,512,4);slices=OSArray::withCapacity(2);
  if(!layout||!bounce||!slices||!raw->open(this,0,kIOStorageAccessReader))return 0;
  i9100_layout_io io={this,read512,raw->getSize()/512U};
  int error=i9100_emmc_read_layout(&io,layout);
  if(!error)error=i9100_emmc_check_pit(&io,layout);
  if(!error){
   const char *names[2]={"FACTORYFS","DATAFS"};
   for(unsigned n=0;n<2;n++){
    const i9100_emmc_partition *p=i9100_emmc_find_partition(layout,names[n]);
    if(!p){error=I9100_LAYOUT_ENTRY;break;}
    i9100_hfs_volume volume;bool hfs=i9100_emmc_hfs_volume(&io,p,&volume)==0;
    IOMedia *child=new IOMedia;
    if(!child){error=I9100_LAYOUT_LIMIT;break;}
    if(!child->init(p->start_lba*512U,p->sectors*512U,512,raw->getAttributes(),false,false,hfs?"Apple_HFS":"Microsoft Basic Data")){
     child->release();error=I9100_LAYOUT_LIMIT;break;
    }
    char location[12];snprintf(location,sizeof(location),"%u",p->gpt_number);
    child->setName(p->name);child->setLocation(location);
    child->setProperty(kIOMediaPartitionIDKey,p->gpt_number,32);
    OSData *guid=OSData::withBytes(p->unique_guid,16);
    if(!guid||!child->setProperty("I9100GPTUniqueGUID",guid)){
     if(guid)guid->release();child->release();error=I9100_LAYOUT_LIMIT;break;
    }
    guid->release();
    child->setProperty("I9100PITIdentifier",p->pit_identifier,32);
    child->setProperty("I9100ProposedRole",n?"data":"system");
    child->setProperty("I9100ReadOnlyInspection",true);
    if(!slices->setObject(child))error=I9100_LAYOUT_LIMIT;
    child->release();if(error)break;
   }
  }
  raw->close(this);
  if(error){IOLog("i9100 GPT/PIT RO: validation failed rc=%d, no target slices published\n",error);return 0;}
  setProperty("Content Mask","GUID_partition_scheme");
  setProperty("I9100GPTHeaderCRC",layout->primary_crc,32);
  setProperty("I9100GPTEntriesCRC",layout->entries_crc,32);
  if(score)*score+=1000;
  return this;
 }
 virtual bool start(IOService *provider){
  if(!IOPartitionScheme::start(provider)||!slices||slices->getCount()!=2)return false;
  for(unsigned n=0;n<slices->getCount();n++){
   IOMedia *child=OSDynamicCast(IOMedia,slices->getObject(n));
   if(!child||!child->attach(this)){
    for(unsigned j=0;j<n;j++){IOMedia *attached=OSDynamicCast(IOMedia,slices->getObject(j));if(attached)attached->detach(this);}
    return false;
   }
  }
  for(unsigned n=0;n<slices->getCount();n++)OSDynamicCast(IOMedia,slices->getObject(n))->registerService();
  return true;
 }
 virtual void stop(IOService *provider){
  if(slices)for(unsigned n=0;n<slices->getCount();n++){
   IOMedia *child=OSDynamicCast(IOMedia,slices->getObject(n));
   if(child&&child->getProvider()==this){child->terminate(kIOServiceRequired);child->detach(this);}
  }
  IOPartitionScheme::stop(provider);
 }
 virtual void free(){
  if(slices)slices->release();if(layout)IOFree(layout,sizeof(*layout));
  if(bounce)bounce->release();if(raw)raw->release();IOPartitionScheme::free();
 }
};
OSDefineMetaClassAndStructors(IOS7I9100GPT,IOPartitionScheme)
void ios7i9100_install_gpt_ro_personality(void)
{
 static const char xml[]=
 "<array><dict><key>IOClass</key><string>IOS7I9100GPT</string>"
 "<key>IOProviderClass</key><string>IOMedia</string>"
 "<key>IOMatchCategory</key><string>IOStorage</string>"
 "<key>IOProbeScore</key><integer>4000</integer>"
 "<key>IOPropertyMatch</key><dict><key>Whole</key><true/></dict>"
 "</dict></array>";
 OSArray *a=OSDynamicCast(OSArray,OSUnserializeXML(xml));
 if(a){gIOCatalogue->addDrivers(a);a->release();}
}
#endif
