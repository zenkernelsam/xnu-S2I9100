#include <cassert>
#include <cstdint>
typedef uint32_t IOReturn;
enum { kIOReturnSuccess=0,kIOReturnBadArgument=0xe00002c2U };
struct IOExternalMethodArguments {
 uint32_t scalarInputCount=0,scalarOutputCount=0,structureInputSize=0,structureOutputSize=0;
 const uint64_t *scalarInput=nullptr;uint64_t *scalarOutput=nullptr;
 void *structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr,*structureVariableOutputData=nullptr;
};
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/I9AksDevelopment.h"
int main()
{
 IOReturn result=123;uint64_t out=0xdeadbeefcafebabeULL,bag=0;
 IOExternalMethodArguments a;a.scalarOutput=&out;a.scalarOutputCount=1;
 assert(!i9_aks_development(17,&a,false,&result) && out==0xdeadbeefcafebabeULL);
 assert(i9_aks_development(17,&a,true,&result) && result==0 && out==3);
 a.scalarInput=&bag;a.scalarInputCount=1;
 assert(i9_aks_development(7,&a,true,&result) && result==0 && out==6);
 bag=1;assert(i9_aks_development(7,&a,true,&result) && result==kIOReturnBadArgument);
 a.scalarInputCount=0;a.structureInputSize=3072;
 assert(i9_aks_development(17,&a,true,&result) && result==kIOReturnBadArgument);
 a.structureInputSize=0;a.scalarOutputCount=0;
 assert(i9_aks_development(17,&a,true,&result) && result==kIOReturnBadArgument);
 a.scalarOutputCount=1;
 assert(i9_aks_development(14,&a,true,&result) && result==0 && out==0);
 const unsigned unsupported[]={8U,9U,10U,11U,27U};
 for(unsigned selector: unsupported)assert(!i9_aks_development(selector,&a,true,&result));
}
