/* SPDX-License-Identifier: BSD-2-Clause */
/* Original IOService matching and IOMedia family match are appended by the
 * runner below. Scheduling/catalogue, card and registry allocation remain
 * explicit host boundaries. No XNU boot or actual HFS mount is claimed. */
#define I9100_EMMC_BOOT_MAIN emmc_contract_fixture_main
#include "i9100_emmc_boot_test.cpp"
#include <libkern/c++/OSString.h>
#include <stdarg.h>

/* Values copied from IOServicePrivate.h, checked by the runner. */
enum {kIOServiceChangesOK=4,kIOServiceInternalDone=8,kIOServiceClassDone=16,kIOServiceInactiveState=1};
#define kIOBSDNameKey "BSD Name"
#define kIOBSDMajorKey "BSD Major"
#define kIOBSDMinorKey "BSD Minor"
#define kIOBSDUnitKey "BSD Unit"
#define kIOMediaContentKey "Content"
#define kIOMediaContentHintKey "Content Hint"
#define kIOMediaEjectableKey "Ejectable"
#define kIOMediaLeafKey "Leaf"
#define kIOMediaOpenKey "Open"
#define kIOMediaPreferredBlockSizeKey "Preferred Block Size"
#define kIOMediaRemovableKey "Removable"
#define kIOMediaSizeKey "Size"
#define kIOMediaUUIDKey "UUID"
#define kIOMediaWholeKey "Whole"
#define kIOMediaWritableKey "Writable"
#define kIOMediaIconKey "Icon"
#define kIOPropertyProtocolCharacteristicsKey "Protocol Characteristics"
#define kIOPropertyPhysicalInterconnectLocationKey "Physical Interconnect Location"
#define kIOPropertyExternalKey "External"
#define kMsgNoWhole "%s: No whole media found for media %s.\n",getName()
#define kMsgNoLocation "%s: No location found for media %s.\n",getName()
enum {kIOMediaAttributeEjectableMask=1,kIOMediaAttributeRemovableMask=2};
class IOMediaBSDClient : public IOService {
public:IOMedia *getWholeMedia(IOMedia *,UInt32 *,char * =nullptr);
};
static const char *gIOProviderClassKey="IOProviderClass",*gIONameMatchKey="IONameMatch",*gIONameMatchedKey="IONameMatched",
 *gIOLocationMatchKey="IOLocationMatch",*gIOPropertyMatchKey="IOPropertyMatch",*gIOPathMatchKey="IOPathMatch",
 *gIORegistryEntryIDKey="IORegistryEntryID",*gIOMatchedServiceCountKey="IOMatchedServiceCount",
 *gIOMatchCategoryKey="IOMatchCategory",*gIOProbeScoreKey="IOProbeScore",*gIOParentMatchKey="IOParentMatch";
static OSBoolean model_true,model_false;
static SInt32 IOServiceObjectOrder(OSDictionary *d,void *)
{auto *n=OSDynamicCast(OSNumber,d->getObject("IOProbeScore"));return n?(SInt32)n->value:0;}
void panic(const char *,...){assert(false);}
static void put_string(OSDictionary *d,const char *key,const char *value)
{auto *s=OSString::withCString(value);assert(s&&d->setObject(key,s));s->release();}
static void put_number(OSDictionary *d,const char *key,uint64_t value)
{auto *n=OSNumber::withNumber(value,32);assert(n&&d->setObject(key,n));n->release();}
static void put_bool(OSDictionary *d,const char *key,bool value)
{assert(d->setObject(key,value?&model_true:&model_false));}
OSDictionary *IOService::dictionaryWithProperties()
{
 auto *d=OSDictionary::withCapacity(strings.size()+numbers.size());if(!d)return nullptr;
 for(auto &entry:strings)put_string(d,entry.first.c_str(),entry.second.c_str());
 for(auto &entry:numbers){if(boolean_keys.count(entry.first))put_bool(d,entry.first.c_str(),entry.second!=0);
  else put_number(d,entry.first.c_str(),entry.second);}
 for(auto &entry:object_properties)assert(d->setObject(entry.first.c_str(),entry.second));return d;
}
void *IOService::metaCast(OSString *name)
{
 if(name->isEqualTo("IOService"))return this;
#define MATCH(type) if(name->isEqualTo(#type))return dynamic_cast<type *>(this)
 MATCH(IOS6I9100EMMCRaw);MATCH(IOS6I9100EMMCMedia);MATCH(IOS6I9100EMMCBlockDriver);
 MATCH(IOS6I9100EMMCStorage);MATCH(IOBlockStorageDevice);MATCH(IOBlockStorageDriver);MATCH(IOMedia);MATCH(IOStorage);
#undef MATCH
 return nullptr;
}
class ExposedEMMCBlockDriver : public IOS6I9100EMMCBlockDriver {
public:IOMedia *createWhole(){char name[]="native whole";return instantiateMediaObject(0,30777344ULL*512U,512,name);}
};
static unsigned operation_calls,operation_fail,raw_stops;
bool i9100_emmc_model_operation(IOService *service,const char *operation)
{
 if(!strcmp(operation,"stop")){if(dynamic_cast<IOS6I9100EMMCRaw *>(service))raw_stops++;return true;}
 return ++operation_calls!=operation_fail;
}
#include "actual-personalities.inc"
static void actual_matches(IOService *service,const bool expected[3])
{
 for(unsigned i=0;i<3;i++){auto *table=actual_personality(i);assert(table);
  assert(service->matchPassive(table,0)==expected[i]);table->release();}
}
static void matching_case(IOService *service,const char *driver,const char *provider,bool expected,
 bool whole_only=false,const char *ancestor=nullptr,uint32_t options=0)
{
 auto *table=OSDictionary::withCapacity(8);put_string(table,"IOClass",driver);put_string(table,"IOProviderClass",provider);
 put_number(table,"IOProbeScore",6000);put_string(table,"IOMatchCategory",driver);
 if(whole_only){auto *props=OSDictionary::withCapacity(2);put_bool(props,"Whole",true);put_bool(props,"Writable",false);
  assert(table->setObject("IOPropertyMatch",props));props->release();}
 if(ancestor){auto *parent=OSDictionary::withCapacity(1);put_string(parent,"IOProviderClass",ancestor);
  assert(table->setObject("IOParentMatch",parent));parent->release();}
 assert(service->matchPassive(table,options)==expected);table->release();
}
static void matching_hierarchy(const char *source)
{
 i9100_emmc_boot_contract c;boot_setup(source,&c);i9100_emmc_native *native=nullptr;
 /* The parent is the real native module, not a synthetic ready flag. */
 i9100_peripheral_native *parent=nullptr;assert(!i9100_peripheral_native_create(1,&parent));
 assert(!i9100_emmc_native_open(1,parent,&profile,I9100_EMMC_N_DATA_RW,&c.card,&native));
 IOService platform;auto *raw=new IOS6I9100EMMCRaw;
 assert(raw&&raw->init()&&raw->bind(1,native)&&raw->attach(&platform)&&raw->start(&platform));
 const bool raw_matches[3]={true,false,false};actual_matches(raw,raw_matches);
 matching_case(raw,"IOS6I9100EMMCBlockDriver","IOS6I9100EMMCRaw",true);
 matching_case(raw,"IOBlockStorageDriver","IOBlockStorageDevice",false);
 ExposedEMMCBlockDriver driver;assert(driver.init(nullptr)&&driver.attach(raw)&&driver.probe(raw,nullptr)==&driver);
 IOMedia *whole=driver.createWhole();assert(OSDynamicCast(IOS6I9100EMMCMedia,whole));
 assert(whole->isWhole()&&!whole->isWritable()&&whole->getSize()==30777344ULL*512U&&whole->attach(&driver));
 const bool whole_matches[3]={false,true,true};actual_matches(whole,whole_matches);
 matching_case(whole,"IOS6I9100EMMCStorage","IOS6I9100EMMCMedia",true,true,"IOS6I9100EMMCBlockDriver");
 matching_case(whole,"IOS6I9100EMMCStorage","IOMedia",true,true,"IOS6I9100EMMCBlockDriver",kIOServiceChangesOK|kIOServiceClassDone);
 matching_case(whole,"IOFDiskPartitionScheme","IOMedia",false,true);
 matching_case(whole,"IOGUIDPartitionScheme","IOMedia",false,true);
 matching_case(whole,"IOS7I9100GPT","IOMedia",false,true);
 matching_case(whole,"IOMediaBSDClient","IOS6I9100EMMCMedia",true,false,"IOS6I9100EMMCRaw");
 matching_case(whole,"IOMediaBSDClient","IOMedia",true); // Preexisting generic BSD category can still work.
 auto *s=new IOS6I9100EMMCStorage;assert(s&&s->init()&&s->attach(whole)&&s->probe(whole,nullptr)==s&&s->start(whole));
 IOMedia *sys=s->copyMedia(1),*dat=s->copyMedia(2);assert(sys&&dat);
 IOMediaBSDClient bsd;
 for(IOMedia *child:{sys,dat}){
  UInt32 size=0;assert(bsd.getWholeMedia(child,&size)==whole&&size==(child==sys?3U:4U));
  char path[8]={0};size=sizeof(path);assert(bsd.getWholeMedia(child,&size,path)==whole);
  assert(!strcmp(path,child==sys?"s9":"s10"));
 }
 for(IOMedia *child:{sys,dat}){
  assert(OSDynamicCast(IOS6I9100EMMCMedia,child));
  const bool slice_matches[3]={false,false,true};actual_matches(child,slice_matches);
  matching_case(child,"IOMediaBSDClient","IOS6I9100EMMCMedia",true,false,"IOS6I9100EMMCRaw");
  matching_case(child,"IOS6I9100EMMCStorage","IOS6I9100EMMCMedia",false,true,"IOS6I9100EMMCBlockDriver");
  matching_case(child,"IOFDiskPartitionScheme","IOMedia",false);
  OSDictionary query;put_string(&query,"IOProviderClass","IOMedia");assert(child->matchPassive(&query,0));
 }
 /* Size/boolean-property checks run the unmodified XNU matchInternal, not
  * a source-text assertion. Wrong ancestor, writable whole and wrong size
  * cannot bind the pair even if its IOClass is spelled correctly. */
 matching_case(whole,"IOS6I9100EMMCStorage","IOS6I9100EMMCMedia",false,true,"IOS6I9100EMMCStorage");
 auto *unrelated=new IOS6I9100EMMCMedia;assert(unrelated->init(0,30777344ULL*512U,512,0,true,false,"")&&unrelated->attach(&platform));
 matching_case(unrelated,"IOS6I9100EMMCStorage","IOS6I9100EMMCMedia",false,true,"IOS6I9100EMMCBlockDriver");
 assert(!s->probe(unrelated,nullptr));unrelated->detach(&platform);unrelated->release();
 whole->writable=true;whole->setProperty("Writable",true);
 matching_case(whole,"IOS6I9100EMMCStorage","IOS6I9100EMMCMedia",false,true,"IOS6I9100EMMCBlockDriver");
 whole->writable=false;whole->setProperty("Writable",false);
 UInt64 saved_size=whole->size;whole->size=512;
 assert(!s->probe(whole,nullptr));whole->size=saved_size;
 assert(!s->open(sys,0,kIOStorageAccessReaderWriter)&&s->open(dat,0,kIOStorageAccessReaderWriter));
 uint64_t tickets[2];sys->setProperty("BSD Major",14U,32);sys->setProperty("BSD Minor",9U,32);
 dat->setProperty("BSD Major",14U,32);dat->setProperty("BSD Minor",10U,32);
 assert(!i9100_emmc_alias_publish(sys,(void *)21,(void *)22,&tickets[0])&&
        !i9100_emmc_alias_publish(dat,(void *)31,(void *)32,&tickets[1]));
 i9100_emmc_alias_commit(sys,tickets[0]);i9100_emmc_alias_commit(dat,tickets[1]);
 IOMedia *root=i9100_emmc_alias_copy_root();assert(root==sys);root->release();
 operation_fail=operation_calls+1;assert(!s->terminate(kIOServiceRequired));operation_fail=0;
 assert(!i9100_emmc_alias_copy_root()&&sys->getProvider()==s&&dat->getProvider()==s);
 i9100_emmc_alias_commit(dat,tickets[1]);assert(!i9100_emmc_alias_copy_root());
 assert(!s->open(dat,0,kIOStorageAccessReaderWriter));
 // Even a rejected termination withdraws readiness and holds live clients.
 s->stop(whole);assert(!sys->terminated&&!dat->terminated);
 assert(i9100_emmc_native_close(&native)==I9100_EMMC_BUSY);
 s->close(dat);operation_fail=operation_calls+1;s->stop(whole);operation_fail=0;
 assert(!sys->terminated&&dat->terminated); // One failed child request retains the tree.
 s->stop(whole);assert(sys->terminated&&dat->terminated);
 assert(sys->getProvider()==s&&dat->getProvider()==s); // terminate acceptance did not finalize.
 assert(i9100_emmc_native_close(&native)==I9100_EMMC_BUSY);
 UInt32 still_rooted=0;assert(bsd.getWholeMedia(dat,&still_rooted)==whole);
 i9100_emmc_alias_unpublish(sys,tickets[0]);i9100_emmc_alias_unpublish(dat,tickets[1]);
 sys->detach(s);dat->detach(s);sys->release();dat->release();s->detach(whole);s->release();
 whole->detach(&driver);whole->release();driver.detach(raw);raw->stop(&platform);raw->detach(&platform);raw->release();
 assert(!i9100_emmc_native_close(&native)&&!i9100_peripheral_native_destroy(&parent));boot_quiet();
}
static void publication_failures(const char *source)
{
 i9100_emmc_boot_contract c;i9100_emmc_boot_owner *owner=nullptr;IOService platform;uint8_t wire[256];
 /* Each raw allocator fails once; neither a raw registration nor a native
  * retain remains. Catalogue/parser failures are independently injected. */
 for(unsigned fault=1;fault<=2;fault++){
  boot_setup(source,&c);operation_calls=operation_fail=raw_stops=0;
  object_calls=0;object_fail=fault;assert(!i9100_emmc_boot_encode(&c,wire));
  assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)&&!owner&&!raw_registered&&!fixture_sector_writes());
  object_fail=0;boot_quiet();
 }
 // Attach/start rejection is cleaned before catalogue or raw registration.
 for(unsigned fault=1;fault<=2;fault++){
  boot_setup(source,&c);operation_calls=raw_stops=0;operation_fail=fault;
  assert(!i9100_emmc_boot_encode(&c,wire));
  assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)&&!owner&&!raw_registered&&!catalogue_calls);
  operation_fail=0;boot_quiet();
 }
 boot_setup(source,&c);operation_calls=operation_fail=raw_stops=0;catalogue_fail=1;
 assert(!i9100_emmc_boot_encode(&c,wire));
 assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)&&!owner&&!raw_registered&&raw_stops==1);boot_quiet();
}
int main()
{
 alarm(20);m.interrupts=true;assert(i9100_emmc_alias_prepare());
 static std::mutex *retained_gate=host_locks.begin()->second;assert(retained_gate);m.locks=0;m.count=0;
 const char *source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(source);
 matching_hierarchy(source);publication_failures(source);
 puts("PASS actual XNU matchInternal/matchPassive/compareProperty+IOMedia family on actual eMMC raw/block/whole/pair: DOS/GPT/generic raw denied, BSD+ancestor/role matching preserved, cleanup/no writes; registry scheduling/card/disk are host facades");
 return 0;
}
