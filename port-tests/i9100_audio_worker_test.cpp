/* SPDX-License-Identifier: BSD-2-Clause; execute real worker with a serial work-loop facade. */
#include "i9100_audio_worker.h"
#include "i9100_pcm_ring.h"
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
static void prepare_worker(i9100_audio_worker *w)
{assert(!i9100_audio_worker_prepare(w,1536000));m.extra_contexts=1;}
int main()
{
 i9100_audio_board_native *b=nullptr;i9100_irq_registry r;i9100_audio_worker *w=nullptr;
 setup_worker(&b,&r);unsigned allocations=m.allocations;
 assert(i9100_audio_worker_create(0,nullptr,nullptr,0,0,0,&w)==I9100_AP_DISABLED&&m.allocations==allocations);
 assert(i9100_audio_worker_create(1,b,&r,1,0,250,&w)==I9100_AP_ARGUMENT);
 m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,1,1,250,&w));
 assert(!(m.gic[0x108/4]&(1U<<3)));assert(i9100_audio_board_close(&b)==I9100_AP_BUSY&&b);
 prepare_worker(w);assert(!i9100_audio_worker_start(w));
 assert(m.gic[0x108/4]&(1U<<3));m.pdma[0x100/4]=1;
 m.interrupts=false;assert(i9100_irq_route(67U|(2U<<10))==1);m.interrupts=true;
 assert(!(m.gic[0x108/4]&(1U<<3)));last_loop->pump();
 struct i9100_audio_worker_status status;assert(!i9100_audio_worker_status(w,&status)&&!status.completed_periods&&status.irq_wakes==1);
 /* SEV preceded DMAEND: the timer supplies a bounded second observation. */
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();
 assert(!i9100_audio_worker_status(w,&status)&&status.completed_periods==1&&!status.fault);
 assert(m.gic[0x108/4]&(1U<<3));
 assert(!i9100_audio_worker_stop(w));unsigned before=status.completed_periods;last_loop->tick();last_loop->pump();
 assert(i9100_audio_worker_enqueue(w,nullptr,0)==I9100_AP_DISABLED);
 assert(!i9100_audio_worker_status(w,&status)&&status.completed_periods==before&&!status.running);
 finish(&w,&b);
 assert(!i9100_irq_route(67U)); /* detached worker cannot receive a stale token */
 for(unsigned point=1;point<=5U;point++){
  setup_worker(&b,&r);event_fail=point;m.extra_contexts=0;
  assert(i9100_audio_worker_create(1,b,&r,0,0,250,&w)!=0&&!w&&!event_objects);
  m.extra_contexts=0;assert(!i9100_audio_board_close(&b));board_released();
 }
 for(unsigned point=1;point<=2U;point++){
  setup_worker(&b,&r);add_fail=point;m.extra_contexts=0;
  assert(i9100_audio_worker_create(1,b,&r,0,0,250,&w)!=0&&!w&&!event_objects);
  m.extra_contexts=0;assert(!i9100_audio_board_close(&b));board_released();
 }
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));
 prepare_worker(w);timer_fail=1;assert(i9100_audio_worker_start(w)==I9100_AP_IO);
 assert(!i9100_audio_worker_status(w,&status)&&status.fault&&!status.running);timer_fail=0;finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,1,1,250,&w));
 prepare_worker(w);assert(!i9100_audio_worker_start(w));irq_mask_fail=1;
 assert(i9100_audio_worker_destroy(&w)==I9100_IRQ_IO&&w&&event_objects&&b);
 assert(i9100_audio_board_close(&b)==I9100_AP_BUSY);irq_mask_fail=0;finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,1,1,250,&w));remove_fail=1;
 assert(i9100_audio_worker_destroy(&w)!=0&&w&&event_objects);assert(i9100_audio_board_close(&b)==I9100_AP_BUSY);
 assert(!i9100_audio_worker_status(w,&status)&&status.detaching&&status.cleanup_error);
 assert(i9100_audio_worker_prepare(w,1536000)==I9100_AP_DISABLED);
 remove_fail=0;finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));
 prepare_worker(w);assert(!i9100_audio_worker_start(w));m.pdma[0x100/4]=1;m.kill_stuck=1;
 assert(i9100_audio_worker_stop(w)!=0&&w);assert(i9100_audio_board_close(&b)==I9100_AP_BUSY);
 m.kill_stuck=0;m.pdma[0x100/4]=0;finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));prepare_worker(w);
 int16_t samples[1024];for(unsigned n=0;n<1024;n++)samples[n]=(int16_t)(n*401U);
 assert(!i9100_audio_worker_enqueue(w,samples,512));assert(!i9100_audio_worker_status(w,&status));
 assert(status.enqueued_frames==512&&status.staged_consumed==128&&status.staged_frames==384&&status.staging_capacity==512);
 assert(i9100_audio_worker_enqueue(w,samples,256)==I9100_RING_FULL);
 assert(!i9100_audio_worker_start(w));
 for(unsigned turn=0;turn<6U;turn++){m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();}
 assert(!i9100_audio_worker_status(w,&status)&&status.completed_periods==6&&status.staged_frames==0&&status.staged_consumed==512);
 finish(&w,&b);
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));prepare_worker(w);
 samples[0]=32767;samples[1]=-32768;assert(!i9100_audio_worker_enqueue(w,samples,1));assert(!i9100_audio_worker_flush(w));
 assert(!i9100_audio_worker_status(w,&status)&&status.staged_frames==0&&status.staged_consumed==1);
 assert(((int16_t *)allocation_bytes[0])[0]==1035&&((int16_t *)allocation_bytes[0])[1]==-1036);
 assert(!i9100_audio_worker_start(w));m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;board_i2s[0]|=1U<<17;
 last_loop->tick();assert(!i9100_audio_worker_status(w,&status));
 assert(status.fault&&!status.running&&status.completed_periods==1&&status.last_pcm_valid&&status.last_pcm.i2s_fault.errors==(1U<<17));
 assert(status.last_pcm.completed_frames==64&&status.last_pcm.last_error==I9100_I2S_FIFO);
 assert(!i9100_audio_worker_stop(w));assert(!i9100_audio_worker_status(w,&status)&&status.last_pcm.i2s_fault_valid);
 finish(&w,&b);
 /* Newly available owned output makes the old timer-fault-only scheduling
  * stop insufficient: real PCM/DMA and codec gain must also be silenced. */
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));prepare_worker(w);
 unsigned output_before=m.transactions;assert(i9100_audio_worker_output_prepare(0,w,I9100_MC_SPEAKER,-30000)==I9100_MC_DISABLED);
 assert(m.transactions==output_before);i9100_audio_worker *other=nullptr;
 assert(i9100_audio_worker_create(1,b,&r,0,0,250,&other)==I9100_AP_BUSY&&!other);
 assert(i9100_audio_board_pcm_close(b)==I9100_AP_BUSY&&i9100_audio_board_output_activate(b)==I9100_AP_BUSY);
 assert(!i9100_audio_worker_output_prepare(1,w,I9100_MC_SPEAKER,-30000));assert(!i9100_audio_worker_start(w));
 assert(i9100_audio_worker_output_activate(w)==I9100_MC_NOT_READY);
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();m.pdma[0x100/4]=1;
 assert(!i9100_audio_worker_output_activate(w));timer_fail=1;last_loop->tick();
 assert(!i9100_audio_worker_status(w,&status)&&status.fault&&!status.running);
 assert(!(m.codec[4][89]&0x7fU)&&!(m.codec[4][90]&0x7fU)&&!(board_i2s[0]&7U));
 timer_fail=0;finish(&w,&b);
 /* Producer data remains in CPU staging during actual zero pre-roll. */
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));prepare_worker(w);
 assert(!i9100_audio_worker_output_prepare(1,w,I9100_MC_HEADPHONES,-30000));
 assert(!i9100_audio_worker_enqueue(w,samples,128));assert(i9100_audio_worker_queue(w,samples,64)==I9100_PCM_BUSY);
 assert(!i9100_audio_worker_status(w,&status)&&status.staged_frames==128&&status.staged_consumed==0);
 assert(!i9100_audio_worker_start(w));
 for(unsigned n=0;n<256U;n++)assert(!allocation_bytes[0][n]);
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();m.pdma[0x100/4]=1;
 assert(!i9100_audio_worker_output_activate(w));
 assert(!i9100_audio_worker_status(w,&status)&&status.output_active&&status.staged_consumed==64);
 assert(!i9100_audio_worker_output_mute(w));assert(!i9100_audio_worker_status(w,&status)&&!status.output_active);
 assert(!(m.codec[4][89]&0x7fU)&&!(m.codec[4][90]&0x7fU));
 assert(!i9100_audio_worker_output_activate(w));finish(&w,&b);
 /* IRQ failure defers silence/close to the workloop, and mask failure never
  * blocks independent DMA/output shutdown. */
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,1,1,250,&w));prepare_worker(w);
 assert(!i9100_audio_worker_output_prepare(1,w,I9100_MC_SPEAKER,-30000));assert(!i9100_audio_worker_start(w));
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();m.pdma[0x100/4]=1;
 assert(!i9100_audio_worker_output_activate(w));unsigned irq_transactions=m.transactions;irq_mask_fail=1;
 m.interrupts=false;assert(i9100_irq_route(67U)==I9100_IRQ_IO);m.interrupts=true; /* hardware mask failure is propagated */
 assert(m.transactions==irq_transactions);last_loop->pump();
 assert(!i9100_audio_worker_status(w,&status)&&status.fault&&status.irq_error==I9100_IRQ_IO&&status.cleanup_error==I9100_IRQ_IO);
 assert(!status.session_owned&&!status.output_requested&&!(board_i2s[0]&7U)&&!(m.codec[4][89]&0x7fU));
 assert(i9100_audio_board_close(&b)==I9100_AP_BUSY);irq_mask_fail=0;finish(&w,&b);
 /* A partial native open and failed I2C cleanup remain owned, rather than
  * disappearing behind a failed prepare return. No automatic recovery. */
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));
 m.board_tx_fail_from=m.transactions+7U;assert(i9100_audio_worker_prepare(w,1536000)!=0);
 assert(!i9100_audio_worker_status(w,&status)&&status.fault&&status.session_owned&&status.cleanup_error);
 assert(i9100_audio_board_close(&b)==I9100_AP_BUSY&&m.maps==8U);
 m.board_tx_fail_from=0;finish(&w,&b);
 /* Post-pre-roll codec IO failure keeps the output and private DMA buffers
  * quarantined until an explicit, verified close. */
 setup_worker(&b,&r);m.extra_contexts=0;assert(!i9100_audio_worker_create(1,b,&r,0,0,250,&w));prepare_worker(w);
 assert(!i9100_audio_worker_output_prepare(1,w,I9100_MC_SPEAKER,-30000));assert(!i9100_audio_worker_start(w));
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();m.pdma[0x100/4]=1;
 m.mute_fail=1;assert(i9100_audio_worker_output_activate(w)!=0);
 assert(!i9100_audio_worker_status(w,&status)&&status.fault&&status.session_owned&&status.output_requested&&status.cleanup_error);
 assert(status.last_board_valid&&status.last_board.output_owned&&m.dma_count&&!(board_i2s[0]&7U));
 m.mute_fail=0;finish(&w,&b);
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 setup_worker(&b,&r);dispatch_registry=&r;i9100_irq_lease lease={};
 assert(!i9100_irq_register_masked(&r,67,0,busy_detach,nullptr,&lease));assert(!i9100_irq_router_attach(&r));
 assert(!i9100_irq_unmask(&lease));m.interrupts=false;assert(i9100_irq_route(67U|(3U<<10))==1);m.interrupts=true;
 assert(!i9100_irq_unregister(&lease));assert(!i9100_irq_router_detach(&r));assert(!i9100_irq_route(67U));
 assert(!i9100_audio_board_close(&b));board_released();
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 puts("PASS real IOEventSource worker/serial work-loop facade: IRQ-only mask+wake/SEV-before-END/timer fallback/single completion/board pin/stop-remove-unhold/failure retention/default OFF");return 0;
}
