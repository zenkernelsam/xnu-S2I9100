/* SPDX-License-Identifier: BSD-2-Clause
 * Host-only facade for the MFC deferred-work model: a real std::thread
 * executes production workloop/event/timer handlers, and explicit register
 * models drive GIC dispatch and firmware replies. No production scheduler,
 * codec, registry or allocator logic is substituted, and nothing here proves
 * actual interrupt delivery or firmware execution on hardware. */
#ifndef I9100_MFC_WORKER_FACADE_H
#define I9100_MFC_WORKER_FACADE_H
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
/* The dma-native descriptor header guards its own IOReturn enum with this
 * name; the canonical enum comes from the events facade below. */
#define I9100_EMMC_STORAGE_COMMON_H
#include <IOKit/IOBufferMemoryDescriptor.h>
#undef I9100_EMMC_STORAGE_COMMON_H
#include <IOKit/IOEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <machine/machine_routines.h>
#include <mach/vm_param.h>
#include <vm/pmap.h>
#include "i9100_irq_registry.h"
#include "i9100_peripheral_native.h"
enum {MF_MFC,MF_MMUL,MF_MMUR,MF_CMU,MF_PMU,MF_CHIP,MF_GIC,MF_MAPS};
static uint32_t mfc_regs[MF_MAPS][131072/4];
static const uint32_t mfc_addresses[MF_MAPS]={0x13400000U,0x13620000U,0x13630000U,0x10030000U,0x10020000U,0x10000000U,0x10490000U};
static struct mfc_model {
 unsigned contexts,locks,maps,descriptors,dma,prepared,barriers,objects,parent_fail;
 unsigned reads,writes,gic_writes,eois,illegal_reads,illegal_writes,in_irq,cmd_writes,ch_writes,mbx_writes;
 std::atomic<uint64_t> ns;bool interrupts;
} m;
static unsigned mfc_fail_count,mfc_fail_at;
static unsigned fail(){return mfc_fail_at&&++mfc_fail_count==mfc_fail_at;}
void *IOMalloc(size_t bytes){if(fail())return nullptr;m.contexts++;return calloc(1,bytes);}
void IOFree(void *p,size_t){assert(p&&m.contexts);m.contexts--;free(p);}
/* Real blocking mutexes with thread-ownership tracking: several model
 * threads contend the same IOLocks (worker guard, board lifecycle, native
 * engine), so Lock must wait, and Unlock must prove the caller owns it. */
struct mfc_lk {std::mutex mu;std::thread::id owner;bool held=false;};
static std::mutex mfc_iolock_table_lock;
static std::map<IOLock *,mfc_lk *> mfc_iolocks;
static mfc_lk *mfc_iolock_mu(IOLock *l)
{std::lock_guard<std::mutex> g(mfc_iolock_table_lock);auto &p=mfc_iolocks[l];if(!p)p=new mfc_lk;return p;}
IOLock *IOLockAlloc(){if(fail())return nullptr;m.locks++;auto *l=new IOLock{false};mfc_iolock_mu(l);return l;}
void IOLockFree(IOLock *l)
{
 assert(l&&!l->held&&m.locks);m.locks--;
 {std::lock_guard<std::mutex> g(mfc_iolock_table_lock);auto it=mfc_iolocks.find(l);
  if(it!=mfc_iolocks.end()){delete it->second;mfc_iolocks.erase(it);}}
 delete l;
}
bool IOLockTryLock(IOLock *l){mfc_lk *k=mfc_iolock_mu(l);if(!k->mu.try_lock())return false;k->owner=std::this_thread::get_id();k->held=l->held=true;return true;}
void IOLockLock(IOLock *l){mfc_lk *k=mfc_iolock_mu(l);k->mu.lock();k->owner=std::this_thread::get_id();k->held=l->held=true;}
void IOLockUnlock(IOLock *l){mfc_lk *k=mfc_iolock_mu(l);assert(l&&l->held&&k->owner==std::this_thread::get_id());k->held=l->held=false;k->owner={};k->mu.unlock();}
void OSSynchronizeIO(){m.barriers++;}
void OSMemoryBarrier(){m.barriers++;}
void IOSleep(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
void IODelay(unsigned us){m.ns+=us*1000ULL;}
static thread_local unsigned mfc_in_irq;
extern "C" int ml_get_interrupts_enabled(){return m.interrupts;}
extern "C" int ml_at_interrupt_context(){return mfc_in_irq;}
extern "C" int ml_set_interrupts_enabled(int enabled){m.interrupts=enabled;return 0;}
extern "C" uint64_t mach_absolute_time(){return m.ns.load();}
extern "C" void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
void *i9100_model_event_allocate(size_t bytes){if(fail())return nullptr;m.objects++;return calloc(1,bytes);}
void i9100_model_event_free(void *p){assert(p&&m.objects);m.objects--;free(p);}
/* Workloop with one real thread, an event source and a timer source. */
struct mfc_loop_state {
 std::recursive_mutex gate;std::mutex mutex;std::condition_variable cv;std::thread thread;bool stopping=false;
};
/* Recursive: addEventSource looks up state under the same lock, and the pump
 * fires timer callbacks while holding it (worker code re-enters
 * signalWorkAvailable/setTimeoutUS from inside the loop thread). */
static std::recursive_mutex mfc_loops_lock;
static std::vector<std::pair<IOWorkLoop *,std::shared_ptr<mfc_loop_state>>> mfc_loops;
static std::vector<std::pair<IOTimerEventSource *,uint64_t>> mfc_timer_deadline;
thread_local IOWorkLoop *i9100_mfc_facade_current_loop;
static std::shared_ptr<mfc_loop_state> mfc_state(IOWorkLoop *loop)
{std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);for(auto &p:mfc_loops)if(p.first==loop)return p.second;return nullptr;}
IOWorkLoop *IOWorkLoop::workLoop(){auto *loop=new IOWorkLoop;if(!loop)return nullptr;return loop;}
/* The loop thread holds a shared_ptr to its state: free() may be invoked on
 * the loop thread itself (worker destroy from a callback), where the join is
 * replaced by a detach and the state must outlive the detached runner. */
static void mfc_loop_spawn(IOWorkLoop *loop,std::shared_ptr<mfc_loop_state> s)
{
 s->thread=std::thread([loop,s]{
  loop->thread=true;i9100_mfc_facade_current_loop=loop;
  for(;;){
   {std::unique_lock<std::mutex> lock(s->mutex);
    s->cv.wait(lock,[s,loop]{return s->stopping||loop->pending;});
    if(s->stopping)break;loop->pending=false;}
   loop->pump();
  }
  loop->thread=false;i9100_mfc_facade_current_loop=nullptr;
 });
}
IOReturn IOWorkLoop::addEventSource(IOEventSource *event)
{
 if(!event||count>=4)return kIOReturnBadArgument;
 std::shared_ptr<mfc_loop_state> st;
 {std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);
  if(!mfc_state(this)){auto s=std::make_shared<mfc_loop_state>();mfc_loops.push_back({this,s});mfc_loop_spawn(this,s);}
  st=mfc_state(this);}
 if(st)st->gate.lock();
 sources[count++]=event;event->retain();event->setWorkLoop(this);
 if(st)st->gate.unlock();return 0;
}
static bool mfc_remove_fail;
IOReturn IOWorkLoop::removeEventSource(IOEventSource *event)
{
 unsigned i=0;while(i<count&&sources[i]!=event)i++;
 if(i==count||!event)return kIOReturnBadArgument;
 if(mfc_remove_fail)return -1; /* provider remove refused: nothing mutated */
 /* Sources[] mutates under the loop's real recursive gate so a caller thread
  * can never free a source the runner is about to dispatch. On the loop
  * thread itself the recursive gate reenters and the removal is immediate. */
 std::shared_ptr<mfc_loop_state> st=mfc_state(this);
 if(st)st->gate.lock();
 while(i+1<count){sources[i]=sources[i+1];i++;}sources[--count]=nullptr;
 {std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);
  for(auto d=mfc_timer_deadline.begin();d!=mfc_timer_deadline.end();)
   d=(d->first==dynamic_cast<IOTimerEventSource *>(event))?mfc_timer_deadline.erase(d):++d;}
 event->setWorkLoop(nullptr);event->release();
 if(st)st->gate.unlock();return 0;
}
IOReturn IOWorkLoop::runAction(Action,OSObject *,void *,void *,void *,void *){return kIOReturnBadArgument;}
/* The loop gate is the state's real recursive mutex: the runner holds it
 * for the whole dispatch pass; callbackGateOpen/Close drop and reacquire
 * exactly one level on the same thread, and every source-table mutation is
 * serialized through it. */
void IOWorkLoop::pump()
{
 std::shared_ptr<mfc_loop_state> st=mfc_state(this);
 if(st)st->gate.lock();gate=true;
 for(unsigned i=0;i<count;i++)
  if(auto *t=dynamic_cast<IOTimerEventSource *>(sources[i])){
   /* Collect under the table lock, fire outside it: firing re-enters the
    * worker (wake→IOLock), which may itself need this table lock. */
   bool due=false;{std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);
    for(auto &d:mfc_timer_deadline)if(d.first==t&&t->queued&&d.second<=m.ns.load()/1000ULL){due=true;break;}}
   if(due)t->fire();
  }
 for(unsigned i=0;i<count;i++){
  IOEventSource *s=sources[i];
  if(!s||dynamic_cast<IOTimerEventSource *>(s))continue; /* sources check their own enable state */
  s->retain();unsigned limit=0,more;
  do{assert(++limit<10000U);more=s->checkForWork()?1U:0U;}while(more);
  s->release();
 }
 gate=false;if(st)st->gate.unlock();
}
/* Worker callbacks open and close exactly one gate level on the runner's
 * own recursive mutex; any other thread attempting it fails the ownership
 * assertion instead of silently unlocking someone else's gate. */
void i9100_mfc_facade_gate_open(IOWorkLoop *loop)
{std::shared_ptr<mfc_loop_state> s=mfc_state(loop);assert(s);s->gate.unlock();}
void i9100_mfc_facade_gate_close(IOWorkLoop *loop)
{std::shared_ptr<mfc_loop_state> s=mfc_state(loop);assert(s);s->gate.lock();}
void IOWorkLoop::tick(){pump();}
void IOWorkLoop::free()
{
 std::shared_ptr<mfc_loop_state> s=mfc_state(this);
 if(s){{std::lock_guard<std::mutex> lock(s->mutex);s->stopping=true;s->cv.notify_all();}
  /* onThread() reports the loop's thread flag, not this caller's context:
   * only the tls pointer answers whether the caller IS the runner. A caller
   * thread must always join; detaching then deleting the loop frees the very
   * object the runner still touches. */
  if(i9100_mfc_facade_current_loop==this){s->thread.detach();}else s->thread.join();
  {std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);
   for(auto p=mfc_loops.begin();p!=mfc_loops.end();++p)if(p->first==this){mfc_loops.erase(p);break;}
   for(auto d=mfc_timer_deadline.begin();d!=mfc_timer_deadline.end();){
    bool owned=false;for(unsigned i=0;i<count;i++)if(d->first==(IOTimerEventSource *)sources[i])owned=true;
    d=owned?mfc_timer_deadline.erase(d):++d;}}}
 /* s drops here; a detached runner keeps its own reference until it exits. */
 assert(!count);OSObject::free();
}
void IOEventSource::signalWorkAvailable()
{if(!workLoop)return;workLoop->pending=true;std::shared_ptr<mfc_loop_state> s=mfc_state(workLoop);if(s){std::lock_guard<std::mutex> lock(s->mutex);s->cv.notify_all();}}
IOReturn IOTimerEventSource::setTimeoutUS(UInt32 us)
{std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);uint64_t deadline=m.ns.load()/1000ULL+us;
 for(auto &d:mfc_timer_deadline)if(d.first==this){d.second=deadline;queued=true;return 0;}
 mfc_timer_deadline.push_back({this,deadline});queued=true;return 0;}
/* Memory/map facade. */
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t pa,size_t bytes,unsigned direction)
{
 if(fail())return nullptr;assert(direction==kIODirectionInOut);m.descriptors++;
 auto *d=new IOMemoryDescriptor;d->physical=pa;d->bytes=bytes;return d;
}
IOMemoryMap *IOMemoryDescriptor::map(unsigned flags)
{
 if(fail())return nullptr;assert(flags==kIOMapInhibitCache);
 for(unsigned i=0;i<MF_MAPS;i++)if(mfc_addresses[i]==physical){
  assert(bytes==(i==MF_CMU?(bytes==131072?131072:65536):i==MF_MFC?65536:i==MF_PMU?16384:4096));
  auto *v=new IOMemoryMap;v->address=(uintptr_t)mfc_regs[i];m.maps++;return v;
 }assert(false);return nullptr;
}
void IOMemoryDescriptor::release(){assert(m.descriptors);m.descriptors--;delete this;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(m.maps);m.maps--;delete this;}
static unsigned mfc_region(volatile uint8_t *base)
{for(unsigned n=0;n<MF_MAPS;n++)if(base==(volatile uint8_t *)mfc_regs[n])return n;assert(false);return 0;}
extern "C" uint32_t __real_i9100_camera_mmio_read(volatile uint8_t *,unsigned);
extern "C" void __real_i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
static bool mfc_mc_stuck;
extern "C" uint32_t __wrap_i9100_camera_mmio_read(volatile uint8_t *base,unsigned off)
{
 unsigned n=mfc_region(base);m.reads++;
 if(n==MF_MFC||n==MF_MMUL||n==MF_MMUR){
  if((mfc_regs[MF_PMU][0x3c44/4]&7U)!=7U||(mfc_regs[MF_CMU][0xc928/4]&7U)!=7U)m.illegal_reads++;
  assert(!m.illegal_reads);
 }
 return __real_i9100_camera_mmio_read(base,off);
}
extern "C" void __wrap_i9100_camera_mmio_write(volatile uint8_t *base,unsigned off,uint32_t v)
{
 unsigned n=mfc_region(base);m.writes++;
 /* Command-traffic accounting: the H2R register and both channel registers
  * issue codec commands, the argument mailbox feeds them. Drain/ack traffic
  * only clears R2H/HOST_INTERRUPT/0x2000 and never touches these offsets. */
 if(n==MF_MFC){
  if(off==0x30)m.cmd_writes++;else if(off==0x2040||off==0x2080)m.ch_writes++;
  else if(off>=0x34&&off<0x44)m.mbx_writes++;
 }
 /* Symmetric to the read side: a write into a clock-gated or powered-down
  * MFC/MMU domain is an illegal access, never a silent drop. */
 if(n==MF_MFC||n==MF_MMUL||n==MF_MMUR){
  if((mfc_regs[MF_PMU][0x3c44/4]&7U)!=7U||(mfc_regs[MF_CMU][0xc928/4]&7U)!=7U)m.illegal_writes++;
  assert(!m.illegal_writes);
 }
 if(n==MF_CMU||n==MF_PMU){unsigned mask=n==MF_PMU&&off==0x3c40?7U:n==MF_CMU&&off==0xc928?7U:n==MF_CMU&&off==0xc228?0x101U:n==MF_CMU&&off==0xc528?15U:0;
  assert(mask&&((v^mfc_regs[n][off/4])&~mask)==0);
 }else assert(n==MF_MFC);
 __real_i9100_camera_mmio_write(base,off,v);
 if(n==MF_PMU&&off==0x3c40)mfc_regs[MF_PMU][0x3c44/4]=(mfc_regs[MF_PMU][0x3c44/4]&~7U)|(v&7U);
 /* A non-0x3ff reset write releases the MC status; mfc_mc_stuck models an
  * MC that ignores reset so a real stop attempt cannot prove quiescence. */
 if(n==MF_MFC&&off==0){if(v==0x3ffU){}else if(!mfc_mc_stuck)mfc_regs[MF_MFC][0x510/4]=0;}
 if(n==MF_MFC&&off==0x30&&v)mfc_regs[MF_MFC][off/4]=0;
}
static uint32_t mfc_irq_read(void *,unsigned off){return mfc_regs[MF_GIC][off/4];}
static bool mfc_gic_mask_fail,mfc_gic_unmask_fail;
static void mfc_irq_write(void *,unsigned off,uint32_t v)
{
 m.gic_writes++;
 assert(off==0x10c||off==0x18c||off==0x47c||off==0x87c||off==0xc1c);
 if(off==0x18c){assert(v==(1U<<30));if(!mfc_gic_mask_fail)mfc_regs[MF_GIC][0x10c/4]&=~v;}
 else if(off==0x10c){assert(v==(1U<<30));if(!mfc_gic_unmask_fail)mfc_regs[MF_GIC][0x10c/4]|=v;}
 else mfc_regs[MF_GIC][off/4]=v;
}
/* The registry enter/leave is its real critical-section spinlock. */
static std::recursive_mutex mfc_irq_spin;
static uint32_t mfc_irq_enter(void *){mfc_irq_spin.lock();return 1;}
static void mfc_irq_leave(void *,uint32_t){mfc_irq_spin.unlock();}
struct i9100_peripheral_native {unsigned holds;i9100_irq_registry irqs;};
static i9100_peripheral_native mfc_parent;
int i9100_peripheral_native_hold_get(i9100_peripheral_native *p,i9100_native_parent_hold *h)
{assert(!h->owned);h->context=p;h->self=(uintptr_t)h;h->owned=1;p->holds++;return 0;}
int i9100_peripheral_native_hold_put(i9100_native_parent_hold *h)
{assert(h->owned&&h->self==(uintptr_t)h&&h->context->holds);if(m.parent_fail)return -99;h->owned=0;h->context->holds--;return 0;}
i9100_irq_registry *i9100_peripheral_native_irqs(i9100_peripheral_native *p){return &p->irqs;}
/* The dispatch provider retains the acknowledge token and performs exactly
 * one EOI: it claims the line active (IAR-equivalent), runs the slot
 * handler under real interrupt context, then completes. A test may defer
 * that completion to model a genuinely late EOI window. */
static bool mfc_eoi_deferred;
static int mfc_dispatch_irq126()
{
 mfc_regs[MF_GIC][0x30c/4]|=1U<<30;
 mfc_in_irq=1;int handled=i9100_irq_dispatch(&mfc_parent.irqs,126);mfc_in_irq=0;
 /* Claiming the line active obligates one EOI even when the handler reports
  * the source already drained (a deferred worker ack can win the race). */
 if(!mfc_eoi_deferred){mfc_regs[MF_GIC][0x30c/4]&=~(1U<<30);m.eois++;}
 return handled;
}
static void mfc_complete_eoi()
{
 /* Exactly one deferred completion: the claimed-active window must still
  * be open; the deferral flag itself may already be re-armed off. */
 assert(mfc_regs[MF_GIC][0x30c/4]&(1U<<30));
 mfc_regs[MF_GIC][0x30c/4]&=~(1U<<30);m.eois++;
}
static uint8_t mfc_bank_a[8388608] __attribute__((aligned(131072))),mfc_bank_b[8388608] __attribute__((aligned(131072)));
static uint32_t mfc_cpu_ptes[2][2048],mfc_linear_ptes[2][2048];static bool mfc_allocated[2];
IOBufferMemoryDescriptor *IOBufferMemoryDescriptor::withOptions(unsigned options,size_t bytes,size_t alignment)
{
 if(fail())return nullptr;assert(options==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapInhibitCache)&&alignment==131072U&&bytes<=8388608U);
 unsigned i=mfc_allocated[0]?1U:0U;assert(!mfc_allocated[i]);mfc_allocated[i]=true;m.dma++;
 auto *d=new IOBufferMemoryDescriptor;d->data=i?mfc_bank_b:mfc_bank_a;d->pa=i?0x48000000U:0x46000000U;d->bytes=bytes;d->prepared=0;return d;
}
IOReturn IOBufferMemoryDescriptor::prepare(unsigned direction){assert(direction==kIODirectionInOut&&!prepared);if(fail())return -1;prepared=1;m.prepared++;return 0;}
IOReturn IOBufferMemoryDescriptor::complete(unsigned direction){assert(direction==kIODirectionInOut&&prepared&&m.prepared);prepared=0;m.prepared--;return 0;}
addr64_t IOBufferMemoryDescriptor::getPhysicalSegment(unsigned off,IOByteCount *span){assert(!off&&prepared);*span=bytes;return pa;}
void *IOBufferMemoryDescriptor::getBytesNoCopy(){return data;}
void IOBufferMemoryDescriptor::release(){assert(!prepared&&m.dma);unsigned i=data==mfc_bank_b;assert(mfc_allocated[i]);mfc_allocated[i]=false;m.dma--;delete this;}
extern "C" {
unsigned long gPhysBase=0x40000000U,gVirtBase=0x80000000U;
pmap_t kernel_pmap=(void *)1;
void pmap_set_cache_attributes(ppnum_t page,unsigned flags)
{assert(flags==(VM_MEM_COHERENT|VM_MEM_NOT_CACHEABLE|VM_MEM_GUARDED));unsigned i=page>=0x48000U;unsigned index=page-(i?0x48000U:0x46000U);assert(index<2048);
 mfc_cpu_ptes[i][index]=mfc_linear_ptes[i][index]=(page<<12)|0x42U;}
vm_offset_t pmap_pte(pmap_t map,vm_offset_t va)
{assert(map==kernel_pmap);for(unsigned i=0;i<2;i++){
 uintptr_t low=(uintptr_t)(i?mfc_bank_b:mfc_bank_a),linear=i?0x88000000U:0x86000000U;
 if(va>=low&&va<low+8388608U)return (uintptr_t)&mfc_cpu_ptes[i][(va-low)/4096U];
 if(va>=linear&&va<linear+8388608U)return (uintptr_t)&mfc_linear_ptes[i][(va-linear)/4096U];}
 return 0;}
int i9100_bcm_fw_kernel_sha256(void *,const void *bytes,uint32_t size,uint8_t out[32]){return SHA256((const uint8_t *)bytes,size,out)?0:-1;}
}
/* Model helpers shared by test scenarios. */
static void mfc_advance_us(unsigned us)
{
 m.ns+=us*1000ULL;
 std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);
 /* Advance the model clock, then wake only the loops that actually own a
  * queued, now-due timer. Waking every loop indiscriminately would let a
  * test pass without any real timer being armed, hiding a missing
  * retirement service (the R004 probe found exactly that). */
 for(auto &d:mfc_timer_deadline){
  if(!d.first->queued||d.second>m.ns.load()/1000ULL)continue;
  for(auto &p:mfc_loops){
   bool owns=false;
   for(unsigned i=0;i<p.first->count;i++)if(p.first->sources[i]==d.first){owns=true;break;}
   if(owns){p.first->pending=true;std::lock_guard<std::mutex> g(p.second->mutex);p.second->cv.notify_all();}
  }
 }
}
static void mfc_setup()
{
 mfc_fail_count=mfc_fail_at=0;mfc_gic_mask_fail=mfc_gic_unmask_fail=mfc_remove_fail=false;mfc_eoi_deferred=false;mfc_in_irq=0;mfc_mc_stuck=false;
 memset(&mfc_parent,0,sizeof(mfc_parent));memset(mfc_regs,0,sizeof(mfc_regs));
 m.contexts=m.locks=m.maps=m.descriptors=m.dma=m.prepared=m.barriers=m.objects=m.parent_fail=0;
 m.reads=m.writes=m.gic_writes=m.eois=m.illegal_reads=m.illegal_writes=m.in_irq=m.cmd_writes=m.ch_writes=m.mbx_writes=0;
 m.ns=1000000;m.interrupts=true;
 assert(mfc_loops.empty()&&mfc_timer_deadline.empty());
 mfc_regs[MF_CHIP][0]=0x43210011;mfc_regs[MF_CMU][0x14200/4]=256;mfc_regs[MF_CMU][0x14108/4]=0xa0640301;
 mfc_regs[MF_CMU][0xc928/4]=0x80000000U;mfc_regs[MF_CMU][0xc228/4]=0x8101;mfc_regs[MF_CMU][0xc528/4]=0x4007;
 mfc_regs[MF_PMU][0x3c40/4]=0x900;mfc_regs[MF_PMU][0x3c44/4]=0x100;
 mfc_regs[MF_GIC][0x47c/4]=0xabcd1234;mfc_regs[MF_GIC][0x87c/4]=0x10203450;mfc_regs[MF_GIC][0xc1c/4]=0xffffffff;
 uint32_t allowed[8]={};allowed[126/32]=1U<<(126%32);i9100_irq_io io={nullptr,mfc_irq_read,mfc_irq_write,mfc_irq_enter,mfc_irq_leave};
 assert(!i9100_irq_registry_init(&mfc_parent.irqs,&io,allowed));assert(!i9100_irq_registry_enable(&mfc_parent.irqs,1));
}
/* Firmware reply model: land a R2H response and raise the host latch; the
 * GIC line reaches the upper half only through an explicit dispatch. */
static void mfc_response(unsigned r,unsigned arg=0)
{mfc_regs[MF_MFC][0x44/4]=r;mfc_regs[MF_MFC][0x48/4]=arg;mfc_regs[MF_MFC][0x4c/4]=mfc_regs[MF_MFC][0x50/4]=mfc_regs[MF_MFC][0x54/4]=0;
 mfc_regs[MF_MFC][8/4]=1;mfc_regs[MF_MFC][0x58/4]=0x211116;}
static bool mfc_wait_idle(unsigned ms) __attribute__((unused));
static bool mfc_wait_idle(unsigned ms)
{
 for(unsigned i=0;i<ms;i++){
  bool busy=false;{std::lock_guard<std::recursive_mutex> lock(mfc_loops_lock);
   for(auto &p:mfc_loops)if(p.first->pending||p.first->gate)busy=true;}
  if(!busy)return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return false;
}
#endif
