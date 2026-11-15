/* SPDX-License-Identifier: BSD-2-Clause; execute real worker with a serial work-loop facade. */
#include "i9100_audio_worker.h"
#include "i9100_irq_router.h"
#include "offline-native-sdk/IOKit/i9100_model_events.h"
#define I9100_BOARD_MODEL_ONLY
#include "i9100_audio_board_test.cpp"
static unsigned event_allocations,event_objects,event_fail,add_calls,add_fail,remove_fail,timer_fail;
static IOWorkLoop *last_loop;
extern "C" int ml_set_interrupts_enabled(int enabled){int old=m.interrupts;m.interrupts=enabled!=0;return old;}
void *i9100_model_event_allocate(size_t bytes)
{if(++event_allocations==event_fail)return nullptr;event_objects++;return calloc(1,bytes);}
void i9100_model_event_free(void *p){assert(event_objects);event_objects--;free(p);}
IOSimpleLock *IOSimpleLockAlloc()
{auto *p=(IOSimpleLock *)i9100_model_event_allocate(sizeof(IOSimpleLock));if(p)p->held=false;return p;}
void IOSimpleLockFree(IOSimpleLock *p){assert(!p->held);i9100_model_event_free(p);}
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *p)
{assert(!p->held);unsigned saved=m.interrupts;p->held=true;m.interrupts=false;return saved;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p,IOInterruptState s)
{assert(p->held);p->held=false;m.interrupts=s!=0;}
IOWorkLoop *IOWorkLoop::workLoop(){last_loop=new IOWorkLoop;return last_loop;}
IOReturn IOWorkLoop::addEventSource(IOEventSource *s)
{if(++add_calls==add_fail)return -99;assert(count<4);s->retain();s->setWorkLoop(this);sources[count++]=s;return 0;}
IOReturn IOWorkLoop::removeEventSource(IOEventSource *s)
{
 if(remove_fail)return -98;unsigned n=0;while(n<count&&sources[n]!=s)n++;if(n==count)return -1;
 s->setWorkLoop(nullptr);s->release();for(unsigned k=n;k+1<count;k++)sources[k]=sources[k+1];sources[--count]=nullptr;return 0;
}
IOReturn IOWorkLoop::runAction(Action action,OSObject *owner,void *a,void *b,void *c,void *d)
{assert(m.interrupts);bool old=gate;gate=true;IOReturn result=action(owner,a,b,c,d);gate=old;return result;}
void IOEventSource::signalWorkAvailable(){if(workLoop)workLoop->pending=true;}
void IOWorkLoop::pump()
{assert(!gate&&m.interrupts);thread=gate=true;pending=false;for(unsigned n=0;n<count;n++)sources[n]->checkForWork();thread=gate=false;}
void IOWorkLoop::tick()
{assert(!gate&&m.interrupts);thread=gate=true;for(unsigned n=0;n<count;n++)if(auto *t=dynamic_cast<IOTimerEventSource *>(sources[n]))t->fire();thread=gate=false;}
void IOWorkLoop::free(){assert(!count&&!gate);}
IOReturn IOTimerEventSource::setTimeoutUS(UInt32 us)
{assert(us>=50&&us<=2000);if(timer_fail)return -97;queued=true;return 0;}
static unsigned irq_locked,irq_mask_fail,irq_enable_fail;
static i9100_irq_registry *dispatch_registry;
static int busy_detach(void *,uint32_t ack)
{assert((ack&1023U)==67U);assert(i9100_irq_router_detach(dispatch_registry)==I9100_IRQ_BUSY);return 0;}
static uint32_t irq_enter(void *){assert(!irq_locked);unsigned s=m.interrupts;irq_locked=1;m.interrupts=false;return s;}
static void irq_leave(void *,uint32_t s){assert(irq_locked);irq_locked=0;m.interrupts=s!=0;}
static uint32_t irq_read(void *,unsigned off){assert(irq_locked);return m.gic[off/4];}
static void irq_write(void *,unsigned off,uint32_t value)
{
 assert(irq_locked);if(off>=0x100U&&off<0x120U){if(!irq_enable_fail)m.gic[off/4]|=value;}
 else if(off>=0x180U&&off<0x1a0U){if(!irq_mask_fail)m.gic[(off-0x80)/4]&=~value;}
 else m.gic[off/4]=value;
}
static void setup_worker(i9100_audio_board_native **b,i9100_irq_registry *r)
{
 assert(!event_objects);board_setup();assert(!open_board(b));memset(r,0,sizeof(*r));
 uint32_t allow[8]={};allow[67/32]|=1U<<(67%32);i9100_irq_io io={nullptr,irq_read,irq_write,irq_enter,irq_leave};
 assert(!i9100_irq_registry_init(r,&io,allow));assert(!i9100_irq_registry_enable(r,1));
 event_allocations=event_fail=add_calls=add_fail=remove_fail=timer_fail=irq_mask_fail=irq_enable_fail=0;
}
static void finish(i9100_audio_worker **w,i9100_audio_board_native **b)
{
 assert(!i9100_audio_worker_destroy(w)&&!(*w)&&!event_objects);m.extra_contexts=0;
 assert(!i9100_audio_board_close(b));board_released();
}
int main()
{
 i9100_audio_board_native *b=nullptr;i9100_irq_registry r;i9100_audio_worker *w=nullptr;
 setup_worker(&b,&r);unsigned allocations=m.allocations;
 assert(i9100_audio_worker_create(0,nullptr,nullptr,0,0,0,&w)==I9100_AP_DISABLED&&m.allocations==allocations);
 assert(i9100_audio_worker_create(1,b,&r,1,0,250,&w)==I9100_AP_ARGUMENT);
 m.extra_contexts=1;assert(!i9100_audio_worker_create(1,b,&r,1,1,250,&w));
 assert(!(m.gic[0x108/4]&(1U<<3)));assert(i9100_audio_board_close(&b)==I9100_AP_BUSY&&b);
 assert(!i9100_audio_worker_prepare(w,1536000));assert(!i9100_audio_worker_start(w));
 assert(m.gic[0x108/4]&(1U<<3));m.pdma[0x100/4]=1;
 m.interrupts=false;assert(i9100_irq_route(67U|(2U<<10))==1);m.interrupts=true;
 assert(!(m.gic[0x108/4]&(1U<<3)));last_loop->pump();
 struct i9100_audio_worker_status status;assert(!i9100_audio_worker_status(w,&status)&&!status.completed_periods&&status.irq_wakes==1);
 /* SEV preceded DMAEND: the timer supplies a bounded second observation. */
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();
 assert(!i9100_audio_worker_status(w,&status)&&status.completed_periods==1&&!status.fault);
 assert(m.gic[0x108/4]&(1U<<3));
 assert(!i9100_audio_worker_stop(w));unsigned before=status.completed_periods;last_loop->tick();last_loop->pump();
 assert(!i9100_audio_worker_status(w,&status)&&status.completed_periods==before&&!status.running);
 finish(&w,&b);
 assert(!i9100_irq_route(67U)); /* detached worker cannot receive a stale token */
 for(unsigned point=1;point<=4U;point++){
  setup_worker(&b,&r);event_fail=point;m.extra_contexts=1;
  assert(i9100_audio_worker_create(1,b,&r,0,0,250,&w)!=0&&!w&&!event_objects);
  m.extra_contexts=0;assert(!i9100_audio_board_close(&b));board_released();
 }
 for(unsigned point=1;point<=2U;point++){
  setup_worker(&b,&r);add_fail=point;m.extra_contexts=1;
  assert(i9100_audio_worker_create(1,b,&r,0,0,250,&w)!=0&&!w&&!event_objects);
  m.extra_contexts=0;assert(!i9100_audio_board_close(&b));board_released();
 }
 setup_worker(&b,&r);m.extra_contexts=1;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));
 assert(!i9100_audio_worker_prepare(w,1536000));timer_fail=1;assert(i9100_audio_worker_start(w)==I9100_AP_IO);
 assert(!i9100_audio_worker_status(w,&status)&&status.fault&&!status.running);timer_fail=0;finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=1;assert(!i9100_audio_worker_create(1,b,&r,1,1,250,&w));
 assert(!i9100_audio_worker_prepare(w,1536000));assert(!i9100_audio_worker_start(w));irq_mask_fail=1;
 assert(i9100_audio_worker_destroy(&w)==I9100_IRQ_IO&&w&&event_objects&&b);
 assert(i9100_audio_board_close(&b)==I9100_AP_BUSY);irq_mask_fail=0;finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=1;assert(!i9100_audio_worker_create(1,b,&r,1,1,250,&w));remove_fail=1;
 assert(i9100_audio_worker_destroy(&w)!=0&&w&&event_objects);assert(i9100_audio_board_close(&b)==I9100_AP_BUSY);
 remove_fail=0;finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=1;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));
 assert(!i9100_audio_worker_prepare(w,1536000));assert(!i9100_audio_worker_start(w));m.pdma[0x100/4]=1;m.kill_stuck=1;
 assert(i9100_audio_worker_stop(w)!=0&&w);assert(i9100_audio_board_close(&b)==I9100_AP_BUSY);
 m.kill_stuck=0;m.pdma[0x100/4]=0;finish(&w,&b);
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 setup_worker(&b,&r);dispatch_registry=&r;i9100_irq_lease lease={};
 assert(!i9100_irq_register_masked(&r,67,0,busy_detach,nullptr,&lease));assert(!i9100_irq_router_attach(&r));
 assert(!i9100_irq_unmask(&lease));m.interrupts=false;assert(i9100_irq_route(67U|(3U<<10))==1);m.interrupts=true;
 assert(!i9100_irq_unregister(&lease));assert(!i9100_irq_router_detach(&r));assert(!i9100_irq_route(67U));
 assert(!i9100_audio_board_close(&b));board_released();
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 puts("PASS real IOEventSource worker/serial work-loop facade: IRQ-only mask+wake/SEV-before-END/timer fallback/single completion/board pin/stop-remove-unhold/failure retention/default OFF");return 0;
}
