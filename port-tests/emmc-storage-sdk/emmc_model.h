/* Isolated host IOKit lifecycle and storage-buffer boundary. */
#ifndef I9100_EMMC_STORAGE_COMMON_H
#define I9100_EMMC_STORAGE_COMMON_H
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <atomic>
#include <vector>
typedef int IOReturn;typedef unsigned UInt32;
enum {kIODirectionInOut=3,kIOMapInhibitCache=0x100,kIOReturnSuccess=0,kIOReturnBadArgument=-1};
struct IOLock {bool held;};
void *IOMalloc(size_t);void IOFree(void *,size_t);
IOLock *IOLockAlloc();void IOLockFree(IOLock *);bool IOLockTryLock(IOLock *);void IOLockUnlock(IOLock *);void IOLockLock(IOLock *);
void OSSynchronizeIO();void IODelay(unsigned);
void *i9100_model_event_allocate(size_t);void i9100_model_event_free(void *);
class OSObject {
 std::atomic<unsigned> refs{1};
public:
 static void *operator new(size_t n) noexcept{return i9100_model_event_allocate(n);}
 static void operator delete(void *p){i9100_model_event_free(p);}
 virtual ~OSObject()=default;virtual bool init(){return true;}virtual void free(){}
 void retain(){assert(refs.fetch_add(1)>0);}void release(){unsigned old=refs.fetch_sub(1);assert(old);if(old==1){free();delete this;}}
 unsigned getRetainCount()const{return refs.load();}
};
#define OSDeclareDefaultStructors(name) public: name();virtual ~name();private:
#define OSDefineMetaClassAndStructors(name,base) name::name()=default;name::~name()=default;
class IOMemoryMap {
public:
 uintptr_t address;uintptr_t getAddress();void release();
};
class IOMemoryDescriptor {
public:
 uint32_t physical=0;size_t bytes=0;unsigned refs=1,direction=1;bool allocated=false;
 std::vector<uint8_t> payload;
 static IOMemoryDescriptor *withPhysicalAddress(uint32_t,size_t,unsigned);
 IOMemoryMap *map(unsigned);size_t getLength(){return payload.size();}
 unsigned getDirection(){return direction;}
 size_t readBytes(size_t,void *,size_t);size_t writeBytes(size_t,const void *,size_t);
 void retain(){refs++;}void release();
};
#endif
