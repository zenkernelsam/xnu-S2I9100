/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_STORAGE_MODEL 1
#define main emmc_native_fixture_test_main
#include "i9100_emmc_native_test.cpp"
#undef main
#include "IOS6I9100EMMCStorage.h"
#include "IOS6I9100EMMCRaw.h"
#include <IOKit/storage/IOBlockStorageDriver.h>
#include "i9100_emmc_alias.h"
#include <condition_variable>
#include <chrono>
#include <unistd.h>
static std::map<IOMedia *,uint64_t> actual_tickets;
static int publish_alias(IOMedia *m,void *b,void *c)
{
 uint64_t ticket=~(uint64_t)0;int e=i9100_emmc_alias_publish(m,b,c,&ticket);
 if(ticket)actual_tickets[m]=ticket;return e;
}
static void unpublish_alias(IOMedia *m)
{auto found=actual_tickets.find(m);assert(found!=actual_tickets.end());i9100_emmc_alias_unpublish(m,found->second);actual_tickets.erase(found);}
static unsigned links,link_fail;static void *handles[4];
extern "C" int devfs_make_link(void *handle,char *name,...)
{assert(handle);links++;if(link_fail==links)return -1;
 assert(!strcmp(name,"i9100-system")||!strcmp(name,"ri9100-system")||!strcmp(name,"i9100-data")||!strcmp(name,"ri9100-data"));
 handles[links%4]=handle;return 0;}
static unsigned objects,object_fail,object_calls,callbacks;static IOReturn completion_error;static uint64_t completion_bytes;
static IOService resource_service;
static std::atomic<unsigned> notifications{0};
static std::mutex notification_mutex;static std::condition_variable notification_cv;
static bool pause_notification=false,notification_entered=false,notification_continue=false;
IOService *IOService::getResourceService(){return &resource_service;}
void IOService::publishResource(const char *k,OSObject *o){assert(o);resource_service.removeProperty(k);o->retain();resource_service.object_properties[k]=o;}
void IOService::registerService()
{
 registered=true;
 if(this==&resource_service){
  notifications.fetch_add(1);std::unique_lock<std::mutex> l(notification_mutex);
  if(pause_notification){notification_entered=true;notification_cv.notify_all();
   assert(notification_cv.wait_for(l,std::chrono::seconds(3),[]{return notification_continue;}));}
 }
}
void *i9100_model_event_allocate(size_t n){if(++object_calls==object_fail)return nullptr;objects++;return calloc(1,n);}
void i9100_model_event_free(void *p){assert(objects);objects--;free(p);}
void IOLockLock(IOLock *l){host_locks.at(l)->lock();assert(!l->held);l->held=true;}
void IOLog(const char *,...){}
size_t IOMemoryDescriptor::readBytes(size_t at,void *out,size_t n)
{if(at>payload.size()||n>payload.size()-at)return 0;memcpy(out,payload.data()+at,n);return n;}
size_t IOMemoryDescriptor::writeBytes(size_t at,const void *in,size_t n)
{if(at>payload.size()||n>payload.size()-at)return 0;memcpy(payload.data()+at,in,n);return n;}
void IOMemoryDescriptor::release()
{assert(refs);if(!--refs){if(allocated){assert(m.descriptors);m.descriptors--;delete this;}}}
static void completed(void *,void *,IOReturn e,UInt64 bytes)
{completion_error=e;completion_bytes=bytes;callbacks++;}
static IOStorageCompletion complete={nullptr,completed,nullptr};
static void submit(IOStorage *s,bool write,IOMedia *client,uint64_t start,IOMemoryDescriptor *buffer)
{callbacks=0;completion_bytes=UINT64_MAX;completion_error=-1000;
 if(write)s->write(client,start,buffer,nullptr,&complete);else s->read(client,start,buffer,nullptr,&complete);
 assert(callbacks==1&&buffer->refs==1);}
int main()
{
 alarm(20);
 /* The kernel-lifetime alias gate is outside each native device's audited
  * allocation window. All device allocation counters still must reach zero. */
 m.interrupts=true;assert(i9100_emmc_alias_prepare());assert(m.locks==1&&host_locks.size()==1);
 static std::mutex *retained_alias_model_mutex=host_locks.begin()->second;assert(retained_alias_model_mutex);
 m.locks=0;m.count=0;
 const char *source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(source);
 setup(source);i9100_emmc_native *native=nullptr;
 i9100_emmc_writer_identity approved;fixture_identity(&approved);
 assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&native));
 auto *s=new IOS6I9100EMMCStorage;assert(s&&s->init());assert(s->bind(1,native));
 IOService platform;auto *raw=new IOS6I9100EMMCRaw;assert(raw&&raw->init()&&raw->bind(1,native)&&raw->attach(&platform)&&raw->start(&platform));
 IOBlockStorageDriver driver;assert(driver.attach(raw));auto *whole=new IOMedia;
 assert(whole&&whole->init(0,30777344ULL*512U,512,0,true,false,"GUID_partition_scheme")&&whole->attach(&driver));
 assert(s->attach(whole)&&s->probe(whole,nullptr)==s&&s->start(whole));
 IOMedia *system=s->copyMedia(1),*data=s->copyMedia(2);assert(system&&data&&!system->whole&&!system->writable&&data->writable);
 assert(system->base==344064ULL*512U&&system->size==1610612736ULL&&data->base==3489792ULL*512U&&data->size==6442450944ULL);
 assert(system->strings["I9100EMMCStableAlias"]=="i9100-system"&&data->strings["I9100EMMCStableAlias"]=="i9100-data");
 assert(!publish_alias(whole,(void *)11,(void *)12)&&!links);
 assert(!publish_alias(system,(void *)21,(void *)22)&&links==2&&system->numbers["I9100EMMCAliasReady"]==0);
 system->setProperty("BSD Major",14U,32);system->setProperty("BSD Minor",9U,32);
 uint64_t system_ticket=i9100_emmc_alias_ticket(system);assert(system_ticket);
 i9100_emmc_alias_commit(system,system_ticket);assert(system->numbers["I9100EMMCAliasReady"]==1);
 assert(resource_service.getProperty("i9100-system-media")==nullptr);
 uint64_t rejected_ticket=42;assert(i9100_emmc_alias_publish(system,(void *)21,(void *)22,&rejected_ticket)==-1&&links==2&&!rejected_ticket);
 i9100_emmc_alias_unpublish(system,rejected_ticket);assert(system->numbers["I9100EMMCAliasReady"]==1);
 unpublish_alias(system);assert(!system->numbers["I9100EMMCAliasReady"]);
 link_fail=links+2;assert(publish_alias(system,(void *)21,(void *)22)==-1&&!system->numbers["I9100EMMCAliasReady"]);
 i9100_emmc_alias_commit(system,system_ticket);assert(!system->numbers["I9100EMMCAliasReady"]&&resource_service.getProperty("i9100-system-media")==nullptr);
 assert(publish_alias(system,(void *)21,(void *)22)==-1);unpublish_alias(system);link_fail=0;
 assert(!publish_alias(system,(void *)21,(void *)22));assert(!publish_alias(data,(void *)31,(void *)32));
 system_ticket=i9100_emmc_alias_ticket(system);uint64_t data_ticket=i9100_emmc_alias_ticket(data);assert(system_ticket&&data_ticket);
 i9100_emmc_alias_commit(system,system_ticket);i9100_emmc_alias_commit(data,data_ticket);
 assert(resource_service.getProperty("i9100-system-media")==system&&resource_service.getProperty("i9100-data-media")==data);
 IOMedia *pinned=i9100_emmc_alias_copy_root();assert(pinned==system);pinned->release();
 uint32_t actual_devices[4]={91,92,93,94};uint64_t creation_tickets[2]={95,96};
 assert(i9100_emmc_alias_devices(actual_devices,creation_tickets)&&actual_devices[0]==91&&creation_tickets[0]==95);
 data->setProperty("BSD Major",14U,32);data->setProperty("BSD Minor",10U,32);
 assert(!i9100_emmc_alias_devices(actual_devices,creation_tickets)&&actual_devices[0]==14&&actual_devices[1]==9&&
  actual_devices[2]==14&&actual_devices[3]==10&&creation_tickets[0]==system_ticket&&creation_tickets[1]==data_ticket);
 unsigned pinned_refs=system->getRetainCount();
 /* commit has finished publication but matching is deliberately paused.
  * Concurrent removal must complete, keep its retained local media valid,
  * and never let the late notification republish a withdrawn pair. */
 {std::lock_guard<std::mutex> l(notification_mutex);pause_notification=true;notification_entered=false;notification_continue=false;}
 std::thread late([&]{system->retain();i9100_emmc_alias_commit(system,system_ticket);system->release();});
 {std::unique_lock<std::mutex> l(notification_mutex);
  assert(notification_cv.wait_for(l,std::chrono::seconds(3),[]{return notification_entered;}));}
 i9100_emmc_alias_invalidate(data);unpublish_alias(data);
 assert(!i9100_emmc_alias_copy_root()&&!resource_service.getProperty("i9100-system-media")&&!resource_service.getProperty("i9100-data-media"));
 assert(i9100_emmc_alias_devices(actual_devices,creation_tickets));
 {std::lock_guard<std::mutex> l(notification_mutex);notification_continue=true;notification_cv.notify_all();}
 late.join();{std::lock_guard<std::mutex> l(notification_mutex);pause_notification=false;}
 assert(system->getRetainCount()+1U==pinned_refs); /* Removed resource retain, local pin dropped. */
 assert(!resource_service.getProperty("i9100-system-media"));
 assert(!publish_alias(data,(void *)31,(void *)32));
 uint64_t fresh_data_ticket=i9100_emmc_alias_ticket(data);assert(fresh_data_ticket!=data_ticket);
 i9100_emmc_alias_commit(data,data_ticket);assert(!i9100_emmc_alias_copy_root());
 i9100_emmc_alias_commit(data,fresh_data_ticket);pinned=i9100_emmc_alias_copy_root();assert(pinned==system);pinned->release();
 i9100_emmc_alias_unpublish(data,data_ticket);pinned=i9100_emmc_alias_copy_root();assert(pinned==system);pinned->release();
 /* Real threads serialize late commit, pair reads and invalidate; slot
  * ownership remains until original node cleanup, so names cannot be stolen. */
 std::atomic<bool> go{false};std::atomic<unsigned> observed{0};
 std::thread commit_race([&]{while(!go.load())std::this_thread::yield();
  for(unsigned i=0;i<64;i++)i9100_emmc_alias_commit(data,fresh_data_ticket);});
 std::thread read_race([&]{while(!go.load())std::this_thread::yield();
  for(unsigned i=0;i<64;i++){IOMedia *r=i9100_emmc_alias_copy_root();if(r){assert(r==system);r->release();observed.fetch_add(1);}}});
 go.store(true);i9100_emmc_alias_invalidate(data);commit_race.join();read_race.join();
 assert(!i9100_emmc_alias_copy_root()&&!resource_service.getProperty("i9100-system-media"));
 assert(publish_alias(data,(void *)31,(void *)32)==-1);
 unpublish_alias(data);assert(!publish_alias(data,(void *)31,(void *)32));
 data_ticket=i9100_emmc_alias_ticket(data);i9100_emmc_alias_commit(data,data_ticket);
 pinned=i9100_emmc_alias_copy_root();assert(pinned==system);pinned->release();
 assert(!s->open(system,0,kIOStorageAccessReaderWriter));assert(s->open(system,0,kIOStorageAccessReader));
 assert(s->open(data,0,kIOStorageAccessReaderWriter));IOService fake;
 assert(!s->open(&fake,0,kIOStorageAccessReaderWriter));
 assert(i9100_emmc_native_close(&native)==I9100_EMMC_BUSY&&native);
 IOMemoryDescriptor buffer;buffer.payload.resize(16384);for(unsigned k=0;k<buffer.payload.size();k++)buffer.payload[k]=(uint8_t)(k*21U+11U);
 const uint64_t lba=3489792ULL+8388608ULL;
 submit(s,true,data,lba*512U,&buffer);assert(!completion_error&&completion_bytes==16384);
 std::vector<uint8_t> expected=buffer.payload;buffer.payload.assign(16384,0);
 submit(s,false,data,lba*512U,&buffer);assert(!completion_error&&buffer.payload==expected);
 unsigned written=fixture_sector_writes();submit(s,true,system,system->base,&buffer);
 assert(completion_error==kIOReturnNotOpen||completion_error==kIOReturnNotWritable);assert(!completion_bytes&&fixture_sector_writes()==written);
 submit(s,true,data,system->base,&buffer);assert(completion_error==kIOReturnBadArgument&&!completion_bytes&&fixture_sector_writes()==written);
 submit(s,true,data,data->base+data->size-512U,&buffer);assert(completion_error==kIOReturnBadArgument&&!completion_bytes);
 assert(s->synchronizeCache(data)==kIOReturnSuccess&&fixture_flushes()==1U);
 assert(s->open(data,0,kIOStorageAccessReader));assert(s->synchronizeCache(data)==kIOReturnNotOpen);
 assert(s->open(data,0,kIOStorageAccessReaderWriter));
 buffer.payload.resize(512);
 submit(s,true,data,lba*512U,&buffer);assert(!completion_error);
 buffer.payload.resize(32768);submit(s,false,data,lba*512U,&buffer);assert(!completion_error&&completion_bytes==32768U);
 IOMemoryDescriptor too_large;too_large.payload.resize(1024U*1024U+512U);
 submit(s,true,data,lba*512U,&too_large);assert(completion_error==kIOReturnBadArgument&&!completion_bytes);
 IOMedia unknown;submit(s,true,&unknown,lba*512U,&buffer);assert(completion_error==kIOReturnNotPermitted);
 buffer.payload.resize(512);m.interrupt_context=true;submit(s,false,data,lba*512U,&buffer);
 assert(completion_error==kIOReturnNotPermitted);m.interrupt_context=false;
 assert(s->discard(data,lba*512U,512U)==kIOReturnUnsupported);
 IOMemoryDescriptor rawBuffer;rawBuffer.payload.resize(512);rawBuffer.direction=kIODirectionOut;
 callbacks=0;assert(raw->doAsyncReadWrite(&rawBuffer,lba,1,nullptr,&complete)==kIOReturnNotWritable&&!callbacks);
 bool protectedMedia=false;assert(!raw->reportWriteProtection(&protectedMedia)&&protectedMedia);
 s->stop(whole); // Open media blocks stop without changing service state.
 IOMedia *extra=s->copyMedia(1);assert(extra);extra->release();
 uint32_t seal_numbers[4];uint64_t seal_tickets[2];assert(!i9100_emmc_alias_devices(seal_numbers,seal_tickets));
 assert(i9100_emmc_alias_seal_data(seal_numbers,seal_tickets)==I9100_EMMC_BUSY); // Live DATAFS cannot seal.
 s->close(data);
 assert(!i9100_emmc_alias_seal_data(seal_numbers,seal_tickets)); // SYSTEM read-only root may remain open.
 assert(!i9100_emmc_alias_copy_root()&&!resource_service.getProperty("i9100-system-media"));
 assert(!s->open(data,0,kIOStorageAccessReaderWriter));unsigned postseal=fixture_sector_writes();
 auto *sealed_provider=new IOS6I9100EMMCStorage;assert(sealed_provider->init()&&!sealed_provider->bind(1,native));sealed_provider->release();
 submit(s,true,data,lba*512U,&buffer);assert(completion_error==kIOReturnNotOpen&&!completion_bytes&&fixture_sector_writes()==postseal);
 assert(i9100_emmc_native_write_data(native,lba,1U,buffer.payload.data())==I9100_EMMC_DIRTY&&fixture_sector_writes()==postseal);
 submit(s,false,system,system->base,&buffer);assert(!completion_error&&completion_bytes==512U); // RO root reads remain legal.
 s->close(system);s->stop(whole);assert(system->terminated&&data->terminated&&!s->copyMedia(1));
 unpublish_alias(system);unpublish_alias(data);assert(actual_tickets.empty());
 assert(resource_service.getProperty("i9100-system-media")==nullptr&&resource_service.getProperty("i9100-data-media")==nullptr);
 system->release();data->release();s->detach(whole);s->release();whole->detach(&driver);whole->release();driver.detach(raw);raw->detach(&platform);raw->release();assert(!objects);
 finish(&native);
 // Read-only native cannot publish a writable data service.
 setup(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&native));
 s=new IOS6I9100EMMCStorage;assert(s->init()&&!s->bind(1,native));s->release();finish(&native);
 assert(notifications.load()>1U);
 puts("PASS actual IOS6 IOMedia/native MMC/write/GPT + retained BSD aliases: real interleaved commit/remove/root threads, no matching under state lock, stale ticket rejected, 6GiBdata/>4GiBwrite/readback/flush, one callback; explicit IOKit/register/media facades, no actual mount/hardware");return 0;
}
