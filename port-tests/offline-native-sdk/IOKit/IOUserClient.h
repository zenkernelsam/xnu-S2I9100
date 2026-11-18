/* Host-only public user-client argument boundary; never an ARM SDK. */
#ifndef I9100_MODEL_USER_CLIENT_H
#define I9100_MODEL_USER_CLIENT_H
#include "IOService.h"
enum {kIOExternalMethodArgumentsCurrentVersion=2};
struct IOExternalMethodArguments {
 uint32_t version=2,selector=0,asyncWakePort=0;uintptr_t *asyncReference=nullptr;uint32_t asyncReferenceCount=0;
 const uint64_t *scalarInput=nullptr;uint32_t scalarInputCount=0;const void *structureInput=nullptr;
 uint32_t structureInputSize=0;IOMemoryDescriptor *structureInputDescriptor=nullptr;
 uint64_t *scalarOutput=nullptr;uint32_t scalarOutputCount=0;void *structureOutput=nullptr;
 uint32_t structureOutputSize=0;IOMemoryDescriptor *structureOutputDescriptor=nullptr;
 uint32_t structureOutputDescriptorSize=0,__reservedA=0;OSObject **structureVariableOutputData=nullptr;
 uint32_t __reserved[30]={};
};
struct IOExternalMethodDispatch {};
class IOUserClient : public IOService {
public:
 virtual bool initWithTask(task_t,void *,UInt32,OSDictionary *){return OSObject::init();}
 virtual IOReturn clientClose(){terminate();return 0;}virtual IOReturn clientDied(){return clientClose();}
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *)
 {return kIOReturnUnsupported;}
};
#endif
