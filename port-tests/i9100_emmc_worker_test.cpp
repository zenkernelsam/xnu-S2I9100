/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_WORKER_TEST_MODEL 1
/* Native parent boundary is small here; MMC/card/DMA/writer remain actual. */
#define I9100_EMMC_STORAGE_MAIN legacy_storage_fixture_main
#include "i9100_emmc_storage_test.cpp"
#include "i9100_emmc_worker.h"
struct completion_record {
 std::mutex mutex;std::condition_variable cv;unsigned count=0;IOReturn result=1;uint64_t bytes=0;
 i9100_emmc_worker *worker=nullptr;OSObject *owner=nullptr;IOMemoryDescriptor *reentrant_buffer=nullptr;
 completion_record *next=nullptr;bool stop_in_callback=false,hold_callback=false;IOReturn stop_result=0;
 IOService *close_storage=nullptr,*close_client=nullptr;
};
static std::atomic<unsigned> validated{0};
static std::mutex chunk_pause_mutex;static std::condition_variable chunk_pause_cv;
static bool chunk_pause=false,chunk_entered=false,chunk_resume=false;
static std::atomic<bool> release_callback_pause{false};
static IOReturn validate_work(OSObject *owner,UInt64 token,unsigned op)
{assert(owner&&token==7U&&op>=1U&&op<=4U);validated.fetch_add(1);return 0;}
static void callback(void *,void *parameter,IOReturn e,UInt64 bytes)
{
 auto *r=(completion_record *)parameter;assert(!emmc_dispatch_gate&&emmc_current_loop);
 if(r->next){IOStorageCompletion next={nullptr,callback,r->next};
  assert(!i9100_emmc_worker_submit(r->worker,r->owner,7U,validate_work,I9100_EMMC_WORK_READ,1,1,r->reentrant_buffer,&next));}
 if(r->stop_in_callback)r->stop_result=i9100_emmc_worker_stop(r->worker);
 if(r->close_storage){auto *storage=(IOS6I9100EMMCStorage *)r->close_storage;storage->close(r->close_client);}
 if(r->hold_callback){while(!release_callback_pause.load())std::this_thread::yield();}
 std::lock_guard<std::mutex> lock(r->mutex);r->result=e;r->bytes=bytes;r->count++;r->cv.notify_all();
}
static IOStorageCompletion completion(completion_record &r){return {nullptr,callback,&r};}
static void wait(completion_record &r)
{std::unique_lock<std::mutex> lock(r.mutex);assert(r.cv.wait_for(lock,std::chrono::seconds(5),[&r]{return r.count!=0;}));assert(r.count==1);}
static void quiet_worker(i9100_emmc_worker **w)
{assert(!i9100_emmc_worker_destroy(w)&&!*w);for(unsigned n=0;emmc_loop_objects&&n<5000;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(!emmc_loop_objects);}
static i9100_emmc_native *open_native(const char *source,bool writing=true,bool dma=false,unsigned failure=0)
{
 setup(source);i9100_emmc_native *n=nullptr;i9100_emmc_writer_identity id;fixture_identity(&id);
 if(dma){dma_model_next=0;extern void worker_model_dma_setup(unsigned);worker_model_dma_setup(failure);}
 assert(!i9100_emmc_native_open_configured(1,&parent,&profile,writing?I9100_EMMC_N_DATA_RW:I9100_EMMC_N_INSPECT,
    writing?&id:nullptr,nullptr,dma?1:0,&n));return n;
}
extern "C" void fixture_dma_buffers(uint8_t *,uint32_t,uint8_t *,uint32_t,unsigned);
extern "C" void fixture_memory_media(uint64_t,unsigned);
void worker_model_dma_setup(unsigned failure)
{fixture_dma_buffers(dma_model_memory,0x46000000,dma_model_memory+4096U,0x46001000U,failure);}
class short_memory : public IOMemoryDescriptor {
public:
 bool fail_read=false,fail_write=false;
 size_t readBytes(size_t at,void *out,size_t n)override{return fail_read?0:IOMemoryDescriptor::readBytes(at,out,n);}
 size_t writeBytes(size_t at,const void *in,size_t n)override{return fail_write?0:IOMemoryDescriptor::writeBytes(at,in,n);}
};
class pause_memory : public IOMemoryDescriptor {
public:
 size_t writeBytes(size_t at,const void *in,size_t n)override{
  size_t bytes=IOMemoryDescriptor::writeBytes(at,in,n);
  std::unique_lock<std::mutex> lock(chunk_pause_mutex);
  if(chunk_pause){chunk_entered=true;chunk_pause_cv.notify_all();assert(chunk_pause_cv.wait_for(lock,std::chrono::seconds(5),[]{return chunk_resume;}));chunk_pause=false;}
  return bytes;
 }
};
struct storage_fixture {
 IOService platform;IOS6I9100EMMCRaw *raw=nullptr;IOS6I9100EMMCBlockDriver driver;
 IOS6I9100EMMCStorage *storage=nullptr;IOMedia *whole=nullptr,*system=nullptr,*data=nullptr;
 bool attach(i9100_emmc_native *native){
  raw=new IOS6I9100EMMCRaw;assert(raw&&raw->init()&&raw->bind(1,native)&&raw->attach(&platform)&&raw->start(&platform));
  assert(driver.attach(raw));whole=new IOS6I9100EMMCMedia;assert(whole&&whole->init(0,30777344ULL*512,512,0,true,false,"GUID_partition_scheme")&&whole->attach(&driver));
  storage=new IOS6I9100EMMCStorage;assert(storage&&storage->init()&&storage->bind(1,native)&&storage->attach(whole)&&storage->probe(whole,nullptr)==storage&&storage->start(whole));
  system=storage->copyMedia(1);data=storage->copyMedia(2);assert(system&&data);return true;
 }
 void destroy(){
  storage->close(system);storage->close(data);storage->stop(whole);system->detach(storage);data->detach(storage);
  system->release();data->release();storage->detach(whole);storage->release();storage=nullptr;
  whole->detach(&driver);whole->release();driver.detach(raw);raw->stop(&platform);raw->detach(&platform);raw->release();raw=nullptr;
  for(unsigned n=0;emmc_loop_objects&&n<5000;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(!emmc_loop_objects);
 }
};
static void storage_cancel_test(const char *source)
{
 i9100_emmc_native *native=open_native(source);storage_fixture f;assert(f.attach(native)&&f.storage->open(f.data,0,kIOStorageAccessReaderWriter));
 emmc_scheduler_paused=1;IOMemoryDescriptor b;b.direction=3;b.payload.resize(512);completion_record r;auto c=completion(r);unsigned before=fixture_sector_writes();
 f.storage->write(f.data,3489792ULL*512,&b,nullptr,&c);assert(!r.count&&b.refs==2);
 f.storage->close(f.data);assert(f.storage->open(f.data,0,kIOStorageAccessReaderWriter));emmc_scheduler_resume();wait(r);
 assert(r.result==kIOReturnAborted&&!r.bytes&&fixture_sector_writes()==before);
 completion_record next;auto n=completion(next);f.storage->write(f.data,3489792ULL*512,&b,nullptr,&n);wait(next);assert(!next.result&&next.bytes==512);
 f.destroy();assert(!i9100_emmc_native_close(&native));fixture_finish();
}
int main(int argc,char **argv)
{
 alarm(40);const char *source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(source);OSObject owner;
 if(argc==2){
  auto *native=open_native(source,true,true);i9100_emmc_worker *worker=nullptr;assert(!i9100_emmc_worker_create(1,native,&worker));
  if(!strcmp(argv[1],"remove-error")){
   emmc_fail_loop_remove=1;assert(i9100_emmc_worker_destroy(&worker)==kIOReturnIOError&&worker);
   assert(i9100_emmc_native_close(&native)==I9100_EMMC_BUSY&&native);
   static i9100_emmc_native *volatile retained_native;retained_native=native;
   static std::mutex *volatile retained_locks[8];unsigned index=0;for(auto &entry:host_locks){assert(index<8);retained_locks[index++]=entry.second;}
   assert(retained_native&&retained_locks[0]);fixture_finish();puts("PASS failed event-source removal retains real worker/native hold without retrying teardown");return 0;
  }
  if(!strcmp(argv[1],"memory-complete-error")){
   static IOMemoryDescriptor *buffer=new IOMemoryDescriptor;buffer->direction=1;buffer->payload.resize(512);buffer->complete_error=kIOReturnIOError;
   static OSObject *memory_owner=new OSObject;completion_record r;auto c=completion(r);
   assert(!i9100_emmc_worker_submit(worker,memory_owner,7,validate_work,I9100_EMMC_WORK_READ,1,1,buffer,&c));wait(r);
   assert(r.result==kIOReturnIOError&&r.bytes==512&&buffer->refs==2);
   i9100_emmc_worker_status s;assert(!i9100_emmc_worker_snapshot(worker,&s)&&s.quarantined==1&&s.fault);
   assert(i9100_emmc_worker_destroy(&worker)==kIOReturnIOError&&worker&&i9100_emmc_native_close(&native)==I9100_EMMC_BUSY);
   static i9100_emmc_native *volatile retained_native;retained_native=native;
   static std::mutex *volatile retained_locks[8];unsigned index=0;for(auto &entry:host_locks){assert(index<8);retained_locks[index++]=entry.second;}
   assert(retained_native&&retained_locks[0]);fixture_finish();puts("PASS failed memory-complete retains prepared descriptor/owner/native hold and reports the original error once");return 0;
  }
  if(!strcmp(argv[1],"hardware-failure")){
   worker_model_dma_setup(2);IOMemoryDescriptor b;b.direction=3;b.payload.resize(512);completion_record r;auto c=completion(r);
   assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_WRITE,3489792ULL,1,&b,&c));wait(r);
   assert(r.result&&r.bytes==0);i9100_emmc_worker_status s;assert(!i9100_emmc_worker_snapshot(worker,&s)&&s.fault&&s.error);
   unsigned before=fixture_sector_writes();completion_record next;auto d=completion(next);
   assert(i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_WRITE,3489792ULL,1,&b,&d)!=0&&!next.count);
   quiet_worker(&worker);assert(i9100_emmc_native_close(&native)!=0&&native&&m.maps==5&&fixture_sector_writes()==before);
   static i9100_emmc_native *volatile retained_native;retained_native=native;
   static std::mutex *volatile retained_locks[8];unsigned index=0;for(auto &entry:host_locks){assert(index<8);retained_locks[index++]=entry.second;}
   assert(retained_native&&retained_locks[0]);fixture_finish();puts("PASS worker hardware error drains callbacks and never retries dirty native/MMC; maps remain held");return 0;
  }
  assert(!strcmp(argv[1],"stop-timeout"));emmc_scheduler_paused=1;IOMemoryDescriptor b;b.direction=3;b.payload.resize(512);
  completion_record r;auto c=completion(r);assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,1,1,&b,&c));
  emmc_wait_timeout=1;assert(i9100_emmc_worker_destroy(&worker)==kIOReturnTimeout&&worker);emmc_wait_timeout=0;
  emmc_scheduler_resume();wait(r);assert(r.result==kIOReturnAborted&&r.bytes==0);
  assert(i9100_emmc_worker_destroy(&worker)==kIOReturnTimeout&&worker);
  assert(i9100_emmc_native_close(&native)==I9100_EMMC_BUSY&&native&&m.maps==5);
  /* Retained failed queue is rooted by production's own fault list. */
  static i9100_emmc_native *volatile retained_native;retained_native=native;
  static std::mutex *volatile retained_locks[8];unsigned index=0;for(auto &entry:host_locks){assert(index<8);retained_locks[index++]=entry.second;}
  assert(retained_native&&retained_locks[0]);fixture_finish();puts("PASS timed-out worker preserves queue/native resources while cancelled callback remains exactly once");return 0;
 }
 assert(argc==1);i9100_emmc_worker *worker=nullptr;
 m.interrupts=true;assert(i9100_emmc_alias_prepare());
 static std::mutex *retained_alias_mutex=host_locks.begin()->second;assert(retained_alias_mutex);m.locks=0;m.count=0;
 assert(i9100_emmc_worker_create(0,nullptr,&worker)==I9100_EMMC_UNSUPPORTED&&!worker);
 storage_cancel_test(source);
 for(unsigned fault=1;fault<=2;fault++){
  auto *native=open_native(source);m.fail_at=m.count+fault;int e=i9100_emmc_worker_create(1,native,&worker);
  assert(e&&!worker);m.fail_at=0;assert(!i9100_emmc_native_close(&native));fixture_finish();assert(!emmc_loop_objects);
 }
 for(unsigned fault=1;fault<=3;fault++){
  auto *n=open_native(source);object_fail=object_calls+fault;int e=i9100_emmc_worker_create(1,n,&worker);
  assert(e&&!worker);object_fail=0;assert(!i9100_emmc_native_close(&n));fixture_finish();assert(!emmc_loop_objects);
 }
 auto *rejected_native=open_native(source);emmc_fail_loop_add=1;
 assert(i9100_emmc_worker_create(1,rejected_native,&worker)==kIOReturnIOError&&!worker);emmc_fail_loop_add=0;
 assert(!i9100_emmc_native_close(&rejected_native));fixture_finish();assert(!emmc_loop_objects);
 auto *native=open_native(source);assert(!i9100_emmc_worker_create(1,native,&worker));
 emmc_scheduler_paused=1;std::vector<std::unique_ptr<IOMemoryDescriptor>> buffers;std::vector<std::unique_ptr<completion_record>> results;
 for(unsigned i=0;i<17;i++){buffers.emplace_back(new IOMemoryDescriptor);buffers.back()->payload.resize(512);buffers.back()->direction=3;results.emplace_back(new completion_record);}
 unsigned before=fixture_writes();
 for(unsigned i=0;i<16;i++){auto c=completion(*results[i]);assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,1,1,buffers[i].get(),&c));}
 auto rejected=completion(*results[16]);assert(i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,1,1,buffers[16].get(),&rejected)==kIOReturnBusy);
 assert(!results[0]->count&&fixture_writes()==before&&owner.getRetainCount()==17U);
 i9100_emmc_worker_status s;assert(!i9100_emmc_worker_snapshot(worker,&s)&&s.queued==16&&!s.active);
 i9100_emmc_worker_cancel(worker,&owner,7);emmc_scheduler_resume();for(unsigned i=0;i<16;i++){wait(*results[i]);assert(results[i]->result==kIOReturnAborted&&!results[i]->bytes);}
 quiet_worker(&worker);assert(owner.getRetainCount()==1U&&fixture_writes()==before);assert(!i9100_emmc_native_close(&native));fixture_finish();
 native=open_native(source);assert(!i9100_emmc_worker_create(1,native,&worker));pause_memory paused;paused.direction=3;paused.payload.resize(32768);
 {std::lock_guard<std::mutex> lock(chunk_pause_mutex);chunk_pause=true;chunk_entered=false;chunk_resume=false;}
 completion_record cancelled;auto pc=completion(cancelled);
 assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,1,64,&paused,&pc));
 {std::unique_lock<std::mutex> lock(chunk_pause_mutex);assert(chunk_pause_cv.wait_for(lock,std::chrono::seconds(5),[]{return chunk_entered;}));}
 i9100_emmc_worker_cancel(worker,&owner,7);
 {std::lock_guard<std::mutex> lock(chunk_pause_mutex);chunk_resume=true;chunk_pause_cv.notify_all();}
 wait(cancelled);assert(cancelled.result==kIOReturnAborted&&cancelled.bytes==16384);
 assert(paused.prepare_calls==1&&paused.complete_calls==1&&!paused.prepared);
 quiet_worker(&worker);assert(!i9100_emmc_native_close(&native));fixture_finish();
 native=open_native(source);assert(!i9100_emmc_worker_create(1,native,&worker));
 emmc_scheduler_paused=1;completion_record held_callback;held_callback.hold_callback=true;release_callback_pause=false;
 IOMemoryDescriptor held_buffer;held_buffer.direction=3;held_buffer.payload.resize(512);auto hc=completion(held_callback);
 assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,1,1,&held_buffer,&hc));emmc_scheduler_resume();
 std::atomic<bool> stop_returned{false};std::thread stopping_thread([&]{assert(!i9100_emmc_worker_stop(worker));stop_returned=true;});
 std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(!stop_returned.load());release_callback_pause=true;
 wait(held_callback);stopping_thread.join();assert(stop_returned);quiet_worker(&worker);assert(!i9100_emmc_native_close(&native));fixture_finish();
 for(unsigned dma=0;dma<2;dma++){
  native=open_native(source,true,dma);assert(!i9100_emmc_worker_create(1,native,&worker));
  const unsigned total=1024U*1024U;uint64_t high=3489792ULL+8388608ULL;fixture_memory_media(high,total/512U);
  IOMemoryDescriptor write,read;write.direction=3;read.direction=3;write.payload.resize(total);read.payload.resize(total);
  for(unsigned k=0;k<write.payload.size();k++)write.payload[k]=(uint8_t)(k*37U);
  completion_record a,b;auto ca=completion(a),cb=completion(b);
  assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_WRITE,high,total/512U,&write,&ca));
  assert(!i9100_emmc_worker_barrier(worker,&owner,7,validate_work,I9100_EMMC_WORK_SYNC));wait(a);
  assert(!a.result&&a.bytes==total&&fixture_flushes()==1);
  assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,high,total/512U,&read,&cb));wait(b);
  assert(!b.result&&b.bytes==total&&read.payload==write.payload);
  assert(write.prepare_calls==1&&write.complete_calls==1&&!write.prepared&&read.prepare_calls==1&&read.complete_calls==1);
  assert(!i9100_emmc_worker_snapshot(worker,&s)&&s.written_bytes==total&&s.read_bytes==total&&s.completed==3);
  IOMemoryDescriptor again;again.direction=3;again.payload.resize(512);completion_record first,second;
  first.worker=worker;first.owner=&owner;first.reentrant_buffer=&again;first.next=&second;
  auto cc=completion(first);assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,1,1,&again,&cc));wait(first);wait(second);
  assert(!first.result&&!second.result);
  completion_record stopping;stopping.worker=worker;stopping.stop_in_callback=true;auto cs=completion(stopping);
  assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_READ,1,1,&again,&cs));wait(stopping);
  assert(stopping.stop_result==kIOReturnBusy);quiet_worker(&worker);assert(!i9100_emmc_native_close(&native));fixture_finish();
 }
 native=open_native(source);auto *raw=new IOS6I9100EMMCRaw;IOService platform;
 assert(raw->init()&&raw->bind(1,native)&&raw->attach(&platform)&&raw->start(&platform));
 IOMemoryDescriptor last;last.direction=1;last.payload.resize(512);completion_record dropped;auto cd=completion(dropped);
 assert(!raw->doAsyncReadWrite(&last,1,1,nullptr,&cd));raw->detach(&platform);raw->release();wait(dropped);
 for(unsigned n=0;emmc_loop_objects&&n<5000;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(!emmc_loop_objects);
 assert(!i9100_emmc_native_close(&native));fixture_finish();
 native=open_native(source);assert(!i9100_emmc_worker_create(1,native,&worker));short_memory bad;bad.direction=3;bad.payload.resize(512);bad.fail_read=true;
 before=fixture_sector_writes();completion_record a;auto c=completion(a);
 assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_WRITE,3489792ULL,1,&bad,&c));wait(a);
 assert(a.result==kIOReturnUnderrun&&!a.bytes&&fixture_sector_writes()==before);
 completion_record b;auto d=completion(b);bad.fail_read=false;bad.prepare_error=kIOReturnNoMemory;
 assert(!i9100_emmc_worker_submit(worker,&owner,7,validate_work,I9100_EMMC_WORK_WRITE,3489792ULL,1,&bad,&d));wait(b);
 assert(b.result==kIOReturnNoMemory&&!b.bytes&&fixture_sector_writes()==before);
 quiet_worker(&worker);assert(!i9100_emmc_native_close(&native));fixture_finish();
 puts("PASS actual async worker/native/MMC: bounded FIFO, no inline IO, prepared memory,>4GiBPIO+IDMAC chunks,orderedflush,reentrantcallbacks,cancellation/backpressure/OOM/shortbuffer/cleanclose; scheduler/MMIO facade only");return 0;
}
