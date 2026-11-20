/* Isolated host storage KPI facade; production ARM includes actual XNU. */
#ifndef I9100_STORAGE_MODEL_H
#define I9100_STORAGE_MODEL_H
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <map>
#include <string>
#include <vector>
typedef uint64_t UInt64;
typedef int32_t SInt32;
typedef unsigned IOOptionBits;
enum {kIODirectionIn=1,kIODirectionOut=2};
enum {kIOReturnNotWritable=-2,kIOReturnBusy=-3,kIOReturnNoMedia=-4,kIOReturnUnsupported=-5,
 kIOReturnIOError=-6,kIOReturnNotPermitted=-7,kIOReturnNotOpen=-8,kIOReturnUnderrun=-9};
enum {kIOReturnAborted=-10,kIOReturnNoMemory=-11,kIOReturnTimeout=-12};
enum {kIOStorageAccessReader=1,kIOStorageAccessReaderWriter=3,kIOServiceRequired=1};
class OSString : public OSObject {
public:
 std::string value;
 explicit OSString(const char *v):value(v){}
 bool isEqualTo(const char *v)const{return value==v;}
 bool isEqualTo(const OSObject *v)const override{auto *s=dynamic_cast<const OSString *>(v);return s&&s->value==value;}
 const char *getCStringNoCopy()const{return value.c_str();}
 static OSString *withCString(const char *value){return new OSString(value);}
 static OSString *withString(OSString *value){return value?new OSString(value->value.c_str()):nullptr;}
};
typedef OSString OSSymbol;
class OSBoolean : public OSObject {};
class OSDictionary : public OSObject {
public:
 using OSObject::isEqualTo;
 std::map<std::string,OSObject *> values;
 ~OSDictionary(){for(auto &entry:values)entry.second->release();}
 static OSDictionary *withCapacity(unsigned){return new OSDictionary;}
 unsigned getCount()const{return values.size();}
 OSObject *getObject(const char *key)const{auto found=values.find(key);return found==values.end()?nullptr:found->second;}
 bool setObject(const char *key,OSObject *value){if(!value)return false;value->retain();auto found=values.find(key);
  if(found!=values.end())found->second->release();values[key]=value;return true;}
 bool isEqualTo(const OSDictionary *other,const OSDictionary *keys)const{
  if(!other||!keys)return false;for(auto &entry:keys->values){auto *ours=getObject(entry.first.c_str());
   auto *theirs=other->getObject(entry.first.c_str());if(!ours||!ours->isEqualTo(theirs))return false;}return true;}
};
class OSIterator : public OSObject {public:virtual OSObject *getNextObject()=0;};
class OSCollection : public OSObject {};
class OSCollectionIterator : public OSIterator {
public:
 static OSIterator *withCollection(OSCollection *){return nullptr;}
 OSObject *getNextObject()override{return nullptr;}
};
class OSNumber : public OSObject {
public:
 uint64_t value=0;bool present=false;
 uint32_t unsigned32BitValue()const{return value;}
 uint64_t unsigned64BitValue()const{return value;}
 static OSNumber *withNumber(uint64_t number,unsigned){auto *n=new OSNumber;n->value=number;n->present=true;return n;}
 bool isEqualTo(const OSObject *v)const override{auto *n=dynamic_cast<const OSNumber *>(v);return n&&n->value==value;}
};
class OSData : public OSObject {
public:
 using OSObject::init;
 std::vector<uint8_t> bytes;
 static OSData *withBytes(const void *p,unsigned n){auto *d=new OSData;if(d)d->bytes.assign((const uint8_t *)p,(const uint8_t *)p+n);return d;}
 unsigned getLength()const{return bytes.size();}const void *getBytesNoCopy()const{return bytes.data();}
};
class IOService;
#if defined(I9100_EMMC_MATCH_ENGINE)
bool i9100_emmc_model_operation(IOService *,const char *);
#endif
class IOService : public OSObject {
 IOService *provider=nullptr;
 OSNumber bsd_major,bsd_minor;
public:
 using OSObject::init;
 std::map<std::string,std::string> strings;
 std::map<std::string,uint64_t> numbers;
 std::map<std::string,bool> boolean_keys;
 std::map<std::string,std::vector<uint8_t>> data;
 std::map<std::string,OSObject *> object_properties;
 std::atomic<bool> registered{false},terminated{false};
 virtual bool init(OSDictionary * =nullptr){return OSObject::init();}
 void free()override{for(auto &entry:object_properties)entry.second->release();object_properties.clear();OSObject::free();}
 virtual bool attach(IOService *p){if(provider)return false;
#if defined(I9100_EMMC_MATCH_ENGINE)
  if(!i9100_emmc_model_operation(this,"attach"))return false;
#endif
  provider=p;p->retain();return true;}
 virtual void detach(IOService *p){if(provider==p){provider=nullptr;p->release();}}
 IOService *getProvider(){return provider;}
 bool isInactive()const{return terminated.load();}
 IOService *copyParentEntry(const void *){if(provider)provider->retain();return provider;}
 virtual bool start(IOService *){
#if defined(I9100_EMMC_MATCH_ENGINE)
  return i9100_emmc_model_operation(this,"start");
#else
  return true;
#endif
 }
 virtual IOService *probe(IOService *,SInt32 *){return this;}
#if defined(I9100_EMMC_MATCH_ENGINE)
 virtual bool matchPropertyTable(OSDictionary *,SInt32 *);
 virtual bool matchPropertyTable(OSDictionary *);
 bool matchInternal(OSDictionary *,uint32_t,uint32_t *);
 bool matchPassive(OSDictionary *,uint32_t);
 bool compareProperty(OSDictionary *,const char *);
 void *metaCast(OSString *);
 bool compareNames(OSObject *,bool,OSString **){return false;}
 bool compareNames(OSObject *,OSString **){return false;}
 OSSymbol *copyLocation(){auto v=strings.find("location");return v==strings.end()?nullptr:OSString::withCString(v->second.c_str());}
 OSDictionary *dictionaryWithProperties();
 OSIterator *getClientIterator(){return nullptr;}
 uint64_t getRegistryEntryID()const{return (uintptr_t)this;}
 IOService *matchLocation(IOService *){return nullptr;}
 uint32_t __state[2]={0};
#else
 virtual bool matchPropertyTable(OSDictionary *,SInt32 *){return true;}
#endif
 virtual void stop(IOService *){
#if defined(I9100_EMMC_MATCH_ENGINE)
  (void)i9100_emmc_model_operation(this,"stop");
#endif
 }
 virtual bool terminate(unsigned){
#if defined(I9100_EMMC_MATCH_ENGINE)
  if(!i9100_emmc_model_operation(this,"terminate"))return false;
#endif
  terminated=true;registered=false;return true;}
 bool setProperty(const char *k,const char *v){strings[k]=v;return true;}
 bool setProperty(const char *k,bool v){numbers[k]=v;boolean_keys[k]=true;return true;}
 bool setProperty(const char *k,uint64_t v,unsigned){numbers[k]=v;
  if(!strcmp(k,"BSD Major")){bsd_major.value=v;bsd_major.present=true;}
  if(!strcmp(k,"BSD Minor")){bsd_minor.value=v;bsd_minor.present=true;}return true;}
 bool setProperty(const char *k,OSData *v){data[k]=v->bytes;return true;}
 bool setProperty(const char *k,OSObject *v){removeProperty(k);v->retain();object_properties[k]=v;return true;}
 void setName(const char *v){strings["name"]=v;}void setLocation(const char *v){strings["location"]=v;}
 const char *getName()const{auto found=strings.find("name");return found==strings.end()?"model-service":found->second.c_str();}
 const char *getLocation()const{auto found=strings.find("location");return found==strings.end()?nullptr:found->second.c_str();}
 void registerService();
 static IOService *getResourceService();static void publishResource(const char *,OSObject *);
 static IOService *fromPath(const char *,const void * =nullptr);
 OSObject *getProperty(const char *k){if(!strcmp(k,"BSD Major"))return bsd_major.present?&bsd_major:nullptr;
  if(!strcmp(k,"BSD Minor"))return bsd_minor.present?&bsd_minor:nullptr;
  auto found=object_properties.find(k);return found==object_properties.end()?nullptr:found->second;}
 OSObject *copyProperty(const char *k){OSObject *o=getProperty(k);if(o)o->retain();return o;}
 void removeProperty(const char *k){auto found=object_properties.find(k);if(found!=object_properties.end()){found->second->release();object_properties.erase(found);}}
};
typedef IOService IORegistryEntry;
static const void * const gIOServicePlane=nullptr;
static const void * const gIODTPlane=nullptr;
struct IOStorageAttributes {uint32_t options,reserved0032;uint64_t reserved0064;};
struct IOStorageCompletion {void *target;void (*action)(void *,void *,IOReturn,UInt64);void *parameter;};
class IOStorage : public IOService {
protected:
 virtual bool handleOpen(IOService *,IOOptionBits,void *)=0;
 virtual bool handleIsOpen(const IOService *)const=0;
 virtual void handleClose(IOService *,IOOptionBits)=0;
public:
 bool open(IOService *client,unsigned options,unsigned access){return handleOpen(client,options,(void *)(uintptr_t)access);}
 void close(IOService *client){handleClose(client,0);}
 bool isOpen(IOService *client=nullptr)const{return handleIsOpen(client);}
 static void complete(IOStorageCompletion *c,IOReturn e,UInt64 bytes){if(c&&c->action)c->action(c->target,c->parameter,e,bytes);}
 virtual void read(IOService *,UInt64,IOMemoryDescriptor *,IOStorageAttributes *,IOStorageCompletion *)=0;
 virtual void write(IOService *,UInt64,IOMemoryDescriptor *,IOStorageAttributes *,IOStorageCompletion *)=0;
 virtual IOReturn synchronizeCache(IOService *)=0;
 virtual IOReturn discard(IOService *,UInt64,UInt64)=0;
};
void IOLog(const char *,...);
#define OSDynamicCast(type,value) dynamic_cast<type *>(value)
#endif
