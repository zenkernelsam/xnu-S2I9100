/* Host-only allocation/PTE facade, never used for ARM compilation. */
#ifndef I9100_DMA_NATIVE_MODEL_DESCRIPTOR
#define I9100_DMA_NATIVE_MODEL_DESCRIPTOR
#include <IOKit/IOMemoryDescriptor.h>
typedef size_t IOByteCount;
typedef uint64_t addr64_t;
typedef int IOReturn;
enum {kIOMemoryPhysicallyContiguous=0x10};
#if !defined(I9100_EMMC_STORAGE_COMMON_H)
enum {kIOReturnSuccess=0};
#endif
class IOBufferMemoryDescriptor: public IOMemoryDescriptor {
public:
 void *data;uint64_t pa;unsigned prepared;
 static IOBufferMemoryDescriptor *withOptions(unsigned,size_t,size_t);
 IOReturn prepare(unsigned);
 IOReturn complete(unsigned);
 addr64_t getPhysicalSegment(unsigned,IOByteCount *);
 void *getBytesNoCopy();
 void release();
};
#endif
