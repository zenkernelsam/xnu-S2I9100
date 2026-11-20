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
typedef uint64_t AbsoluteTime;
enum {kIODirectionInOut=3,kIOMapInhibitCache=0x100,kIOReturnSuccess=0,kIOReturnBadArgument=-1};
struct IOLock {bool held;};
void *IOMalloc(size_t);void IOFree(void *,size_t);
IOLock *IOLockAlloc();void IOLockFree(IOLock *);bool IOLockTryLock(IOLock *);void IOLockUnlock(IOLock *);void IOLockLock(IOLock *);
void OSSynchronizeIO();void IODelay(unsigned);
void OSMemoryBarrier();
int IOLockSleepDeadline(IOLock *,void *,uint64_t,unsigned);void IOLockWakeup(IOLock *,void *,bool);
void IOLog(const char *,...);
void *i9100_model_event_allocate(size_t);void i9100_model_event_free(void *);
class OSObject {
 mutable std::atomic<unsigned> refs{1};
public:
 static void *operator new(size_t n) noexcept{return i9100_model_event_allocate(n);}
 static void operator delete(void *p){i9100_model_event_free(p);}
 virtual ~OSObject()=default;virtual bool init(){return true;}virtual void free(){}
 virtual bool deferDeletion()const{return false;}
 virtual bool isEqualTo(const OSObject *other)const{return this==other;}
 void retain()const{assert(refs.fetch_add(1)>0);}void release()const{unsigned old=refs.fetch_sub(1);assert(old);if(old==1){auto *self=const_cast<OSObject *>(this);self->free();if(!self->deferDeletion())delete self;}}
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
 virtual ~IOMemoryDescriptor()=default;
 uint32_t physical=0;size_t bytes=0;std::atomic<unsigned> refs{1};unsigned direction=1;bool allocated=false;
 std::vector<uint8_t> payload;
 static IOMemoryDescriptor *withPhysicalAddress(uint32_t,size_t,unsigned);
 IOMemoryMap *map(unsigned);size_t getLength(){return payload.size();}
 unsigned getDirection(){return direction;}
 unsigned prepared=0,prepare_calls=0,complete_calls=0;int prepare_error=0,complete_error=0;
 virtual IOReturn prepare(unsigned direction=3){prepare_calls++;if(prepare_error)return prepare_error;assert(direction==1||direction==2||direction==3);prepared++;return 0;}
 virtual IOReturn complete(unsigned direction=3){complete_calls++;assert(prepared&&(direction==1||direction==2||direction==3));prepared--;return complete_error;}
 virtual size_t readBytes(size_t,void *,size_t);virtual size_t writeBytes(size_t,const void *,size_t);
 void retain(){refs++;}void release();
};
#endif
