/* Host-only service lifecycle facade. The ARM build uses the real XNU header. */
#ifndef I9100_MODEL_SERVICE_H
#define I9100_MODEL_SERVICE_H
#include "i9100_model_events.h"
typedef void *task_t;
class OSDictionary {};class IOUserClient;
enum {kIOReturnNoMemory=-2,kIOReturnError=-3,kIOReturnExclusiveAccess=-4,
 kIOReturnNotReady=-5,kIOReturnIOError=-6,kIOReturnUnsupported=-7,
 kIOReturnNotAttached=-8,kIOReturnNotPermitted=-9};
void IOLockLock(IOLock *);
class IOService : public OSObject {
 IOService *provider=nullptr;
public:
 bool registered=false,terminated=false;
 virtual bool attach(IOService *p){if(provider)return false;provider=p;p->retain();return true;}
 virtual void detach(IOService *p){if(provider==p){provider=nullptr;p->release();}}
 virtual bool start(IOService *){return true;}virtual void stop(IOService *){}
 virtual bool terminate(){terminated=true;registered=false;return true;}
 virtual IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **){return kIOReturnUnsupported;}
 IOService *getProvider(){return provider;}
 void setName(const char *){}void setProperty(const char *,uint64_t,unsigned){}void setProperty(const char *,bool){}
 void registerService(){registered=true;}
 void free() override{if(provider)detach(provider);OSObject::free();}
};
#endif
