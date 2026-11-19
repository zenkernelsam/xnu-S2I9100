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
enum {kIOStorageAccessReader=1,kIOStorageAccessReaderWriter=3,kIOServiceRequired=1};
class OSDictionary {};
class OSData : public OSObject {
public:
 using OSObject::init;
 std::vector<uint8_t> bytes;
 static OSData *withBytes(const void *p,unsigned n){auto *d=new OSData;if(d)d->bytes.assign((const uint8_t *)p,(const uint8_t *)p+n);return d;}
};
class IOService : public OSObject {
 IOService *provider=nullptr;
public:
 using OSObject::init;
 std::map<std::string,std::string> strings;
 std::map<std::string,uint64_t> numbers;
 std::map<std::string,std::vector<uint8_t>> data;
 std::map<std::string,OSObject *> object_properties;
 std::atomic<bool> registered{false},terminated{false};
 virtual bool init(OSDictionary * =nullptr){return OSObject::init();}
 virtual bool attach(IOService *p){if(provider)return false;provider=p;p->retain();return true;}
 virtual void detach(IOService *p){if(provider==p){provider=nullptr;p->release();}}
 IOService *getProvider(){return provider;}
 IOService *copyParentEntry(const void *){if(provider)provider->retain();return provider;}
 virtual bool start(IOService *){return true;}
 virtual IOService *probe(IOService *,SInt32 *){return this;}
 virtual void stop(IOService *){}
 virtual bool terminate(unsigned){terminated=true;registered=false;return true;}
 bool setProperty(const char *k,const char *v){strings[k]=v;return true;}
 bool setProperty(const char *k,bool v){numbers[k]=v;return true;}
 bool setProperty(const char *k,uint64_t v,unsigned){numbers[k]=v;return true;}
 bool setProperty(const char *k,OSData *v){data[k]=v->bytes;return true;}
 bool setProperty(const char *k,OSObject *v){removeProperty(k);v->retain();object_properties[k]=v;return true;}
 void setName(const char *v){strings["name"]=v;}void setLocation(const char *v){strings["location"]=v;}
 void registerService();
 static IOService *getResourceService();static void publishResource(const char *,OSObject *);
 OSObject *getProperty(const char *k){auto found=object_properties.find(k);return found==object_properties.end()?nullptr:found->second;}
 void removeProperty(const char *k){auto found=object_properties.find(k);if(found!=object_properties.end()){found->second->release();object_properties.erase(found);}}
};
typedef IOService IORegistryEntry;
static const void * const gIOServicePlane=nullptr;
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
