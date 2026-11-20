/* SPDX-License-Identifier: BSD-2-Clause
 * Scheduler/IOKit boundary only: a real std::thread executes production
 * worker/event handlers. No native MMC/read/write code is substituted. */
#include <IOKit/IOWorkLoop.h>
#include <kern/clock.h>
#include <chrono>
#include <memory>
#include <algorithm>
static std::mutex emmc_loop_list_lock;
static std::vector<IOWorkLoop *> emmc_loops;
static std::atomic<unsigned> emmc_scheduler_paused{0},emmc_loop_objects{0};
static std::atomic<unsigned> emmc_pinned_stop_calls{0};
static unsigned emmc_fail_loop_add,emmc_fail_loop_remove,emmc_wait_timeout;
static thread_local IOWorkLoop *emmc_current_loop;
static thread_local bool emmc_dispatch_gate;
struct IOWorkLoop::Impl {
 std::recursive_mutex gate;std::mutex mutex;std::condition_variable cv;
 std::thread thread;IOEventSource *source=nullptr;bool pending=false,stopping=false;
};
void IOWorkLoop::closeGate(){impl->gate.lock();emmc_dispatch_gate=true;}
void IOWorkLoop::openGate(){emmc_dispatch_gate=false;impl->gate.unlock();}
bool IOWorkLoop::onThread()const{return emmc_current_loop==this;}
IOWorkLoop *IOWorkLoop::workLoop()
{
 auto *loop=new IOWorkLoop;if(!loop)return nullptr;if(!loop->init()){loop->release();return nullptr;}return loop;
}
bool IOWorkLoop::init()
{
 auto *loop=this;loop->impl=new Impl;
 emmc_loop_objects.fetch_add(1);
 {std::lock_guard<std::mutex> lock(emmc_loop_list_lock);emmc_loops.push_back(loop);}
 loop->impl->thread=std::thread([loop]{
  emmc_current_loop=loop;auto *s=loop->impl;
  for(;;){
   {std::unique_lock<std::mutex> lock(s->mutex);
    s->cv.wait(lock,[s]{return s->stopping||(s->pending&&!emmc_scheduler_paused.load());});
    if(s->stopping)break;s->pending=false;}
   loop->closeGate();IOEventSource *event=s->source;
   if(event){event->retain();unsigned limit=0;bool more;
    do{assert(++limit<10000U);more=event->checkForWork();}while(more);
    event->release();}
   loop->openGate();
  }
  emmc_current_loop=nullptr;if(loop->deferred){loop->finishModelThread();delete loop;emmc_loop_objects.fetch_sub(1);}
 });return true;
}
IOReturn IOWorkLoop::addEventSource(IOEventSource *event)
{
 if(emmc_fail_loop_add)return kIOReturnIOError;std::lock_guard<std::recursive_mutex> lock(impl->gate);
 if(!event||impl->source)return kIOReturnBusy;impl->source=event;event->retain();event->setWorkLoop(this);return 0;
}
IOReturn IOWorkLoop::removeEventSource(IOEventSource *event)
{
 if(emmc_fail_loop_remove)return kIOReturnIOError;std::lock_guard<std::recursive_mutex> lock(impl->gate);
 if(event!=impl->source)return kIOReturnBadArgument;impl->source=nullptr;event->setWorkLoop(nullptr);event->release();return 0;
}
void IOWorkLoop::signalWorkAvailable()
{std::lock_guard<std::mutex> lock(impl->mutex);impl->pending=true;impl->cv.notify_all();}
void IOEventSource::signalWorkAvailable(){assert(workLoop);workLoop->signalWorkAvailable();}
void IOEventSource::openGate(){assert(workLoop);workLoop->openGate();}
void IOEventSource::closeGate(){assert(workLoop);workLoop->closeGate();}
void IOWorkLoop::free()
{
 if(!impl){OSObject::free();return;}
 {std::lock_guard<std::mutex> lock(impl->mutex);impl->stopping=true;impl->cv.notify_all();}
 if(onThread()){deferred=true;impl->thread.detach();return;}
 emmc_pinned_stop_calls.fetch_add(1);impl->thread.join();finishModelThread();emmc_loop_objects.fetch_sub(1);emmc_pinned_stop_calls.fetch_sub(1);
}
void IOWorkLoop::finishModelThread()
{
 assert(!impl->source);
 {std::lock_guard<std::mutex> lock(emmc_loop_list_lock);
  auto found=std::find(emmc_loops.begin(),emmc_loops.end(),this);assert(found!=emmc_loops.end());emmc_loops.erase(found);}
 delete impl;impl=nullptr;OSObject::free();
}
static void __attribute__((unused)) emmc_scheduler_resume()
{
 emmc_scheduler_paused=0;std::lock_guard<std::mutex> lock(emmc_loop_list_lock);
 for(auto *loop:emmc_loops){std::lock_guard<std::mutex> guard(loop->impl->mutex);loop->impl->cv.notify_all();}
}
static std::mutex emmc_sleep_lock;
static std::condition_variable emmc_sleep_cv;
static uint64_t emmc_sleep_generation;
extern "C" void clock_interval_to_deadline(uint32_t interval,uint32_t scale,uint64_t *out)
{assert(interval==60U&&scale==kSecondScale);*out=(uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
 std::chrono::steady_clock::now().time_since_epoch()).count()+5000000000ULL;}
int IOLockSleepDeadline(IOLock *lock,void *event,uint64_t deadline,unsigned kind)
{
 assert(event&&kind==THREAD_UNINT&&lock->held);
 std::unique_lock<std::mutex> sleep(emmc_sleep_lock);uint64_t version=emmc_sleep_generation;
 IOLockUnlock(lock);bool ready;
 if(emmc_wait_timeout)ready=false;
 else ready=emmc_sleep_cv.wait_until(sleep,std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline)),
   [version]{return emmc_sleep_generation!=version;});
 sleep.unlock();IOLockLock(lock);return ready?0:THREAD_TIMED_OUT;
}
void IOLockWakeup(IOLock *lock,void *event,bool)
{assert(lock->held&&event);std::lock_guard<std::mutex> sleep(emmc_sleep_lock);emmc_sleep_generation++;emmc_sleep_cv.notify_all();}
