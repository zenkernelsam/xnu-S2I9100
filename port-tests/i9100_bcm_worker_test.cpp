/* SPDX-License-Identifier: BSD-2-Clause
 * Production worker/pump/FW/CDC state, host IOKit facade and a separate
 * native-hold API stand-in. Native implementation is verified in its own
 * fixture; this test does not claim native SDHCI enumeration or real IRQs. */
#include "i9100_bcm_worker.h"
#include "offline-native-sdk/IOKit/i9100_model_events.h"
#include <typeinfo>
#include <stdlib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "i9100_bcm_runtime_model.h"
#pragma clang diagnostic pop
static unsigned allocated,live_objects,fail_allocate,add_calls,fail_add,fail_remove,fail_timer;
static IOWorkLoop *last_loop;
static bool interrupts=true;
static i9100_native_transport_lease host;
static i9100_native_transport_hold session;
static unsigned host_holds,hold_busy,hold_fault,put_fail,rx_delivered,close_in_callback;
static char parent_tag;
static i9100_bcm_worker *worker;
void *i9100_model_event_allocate(size_t n){if(++allocated==fail_allocate)return nullptr;live_objects++;return calloc(1,n);}
void i9100_model_event_free(void *p){assert(p&&live_objects);live_objects--;free(p);}
void *IOMalloc(size_t n){return i9100_model_event_allocate(n);}
void IOFree(void *p,size_t){if(p)i9100_model_event_free(p);}
IOLock *IOLockAlloc(){auto *p=(IOLock *)i9100_model_event_allocate(sizeof(IOLock));if(p)p->held=false;return p;}
void IOLockFree(IOLock *p){assert(!p->held);i9100_model_event_free(p);}
bool IOLockTryLock(IOLock *p){if(p->held)return false;p->held=true;return true;}
void IOLockUnlock(IOLock *p){assert(p->held);p->held=false;}
IOSimpleLock *IOSimpleLockAlloc(){auto *p=(IOSimpleLock *)i9100_model_event_allocate(sizeof(IOSimpleLock));if(p)p->held=false;return p;}
void IOSimpleLockFree(IOSimpleLock *p){assert(!p->held);i9100_model_event_free(p);}
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *p){assert(!p->held);unsigned old=interrupts;interrupts=false;p->held=true;return old;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p,IOInterruptState old){assert(p->held);p->held=false;interrupts=old!=0;}
extern "C" int ml_get_interrupts_enabled(){return interrupts;}
extern "C" int ml_set_interrupts_enabled(int n){int old=interrupts;interrupts=n!=0;return old;}
extern "C" uint64_t mach_absolute_time(){return m.ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t n,uint64_t *out){*out=n;}
void IODelay(unsigned us){m.ns+=(uint64_t)us*1000U;}
void OSSynchronizeIO(){}
IOWorkLoop *IOWorkLoop::workLoop(){last_loop=new IOWorkLoop;return last_loop;}
IOReturn IOWorkLoop::addEventSource(IOEventSource *s){if(++add_calls==fail_add)return -99;assert(count<4);s->retain();s->setWorkLoop(this);sources[count++]=s;return 0;}
IOReturn IOWorkLoop::removeEventSource(IOEventSource *s)
{if(fail_remove)return -98;unsigned n=0;while(n<count&&sources[n]!=s)n++;assert(n<count);s->setWorkLoop(nullptr);s->release();for(unsigned k=n;k+1<count;k++)sources[k]=sources[k+1];sources[--count]=nullptr;return 0;}
IOReturn IOWorkLoop::runAction(Action action,OSObject *owner,void *a,void *b,void *c,void *d)
{assert(interrupts&&owner&&typeid(*owner)!=typeid(OSObject));bool old=gate;gate=true;IOReturn e=action(owner,a,b,c,d);gate=old;return e;}
void IOEventSource::signalWorkAvailable(){assert(workLoop);workLoop->pending=true;}
void IOWorkLoop::pump(){assert(!gate&&interrupts);thread=gate=true;pending=false;for(unsigned n=0;n<count;n++)sources[n]->checkForWork();thread=gate=false;}
void IOWorkLoop::tick(){assert(!gate&&interrupts);thread=gate=true;for(unsigned n=0;n<count;n++)if(auto *t=dynamic_cast<IOTimerEventSource *>(sources[n]))t->fire();thread=gate=false;}
void IOWorkLoop::free(){assert(!count&&!gate);}
IOReturn IOTimerEventSource::setTimeoutUS(UInt32 n){assert(n>=500U&&n<=10000U);if(fail_timer)return -97;queued=true;return 0;}
/* Stand-in for already separately tested native lease calls. Never substituted
 * for production pump, BCM/CDC, firmware or controller functions. */
int i9100_peripheral_native_transport_hold_get(i9100_native_transport_lease *lease,i9100_native_transport_hold *h)
{assert(interrupts);if(hold_busy)return I9100_GPIO_BUSY;if(lease!=&host||h->owned)return I9100_GPIO_ARGUMENT;h->lease=lease;h->self=(uintptr_t)h;h->kind=I9100_NATIVE_SDIO;h->generation=host.generation;h->owned=1;host_holds++;return 0;}
int i9100_peripheral_native_transport_hold_check(i9100_native_transport_hold *h)
{if(hold_busy)return I9100_GPIO_BUSY;if(hold_fault)return I9100_GPIO_IO;return h&&h->owned&&h->self==(uintptr_t)h&&h->lease==&host&&h->generation==host.generation?0:I9100_GPIO_OWNER;}
int i9100_peripheral_native_transport_hold_put(i9100_native_transport_hold *h)
{if(put_fail)return I9100_GPIO_BUSY;assert(h&&h->owned&&h->self==(uintptr_t)h&&host_holds>1U);h->owned=0;host_holds--;return 0;}
static int packet_lock(void *cookie){assert(cookie==&host);return lock(&m);}
static void packet_unlock(void *cookie){assert(cookie==&host);unlock(&m);}
static uint64_t packet_now(void *cookie){assert(cookie==&host);return now(&m);}
static int packet_delay(void *cookie,uint32_t us){assert(cookie==&host);return delay(&m,us);}
static int receive_data(void *,const uint8_t *p,unsigned bytes,unsigned priority)
{assert(last_loop->thread&&last_loop->gate&&!m.locked&&bytes==64U&&priority==0U&&p[14]==0x5a);rx_delivered++;
 if(close_in_callback)assert(i9100_bcm_worker_destroy(&worker)==I9100_PUMP_BUSY&&worker);return 0;}
static i9100_bcm_output output={nullptr,receive_data};
static uint8_t sequence;
static void start_host()
{
 assert(!live_objects);setup_boot();assert(!i9100_bcm_boot_start(&boot));
 host={};session={};host.context=(i9100_peripheral_native *)&parent_tag;host.self=(uintptr_t)&host;
 host.kind=I9100_NATIVE_SDIO;host.generation=host.owned=1;session.lease=&host;session.self=(uintptr_t)&session;
 session.kind=I9100_NATIVE_SDIO;session.generation=session.owned=1;host_holds=1;
 packets.io.cookie=&host;packets.io.lock=packet_lock;packets.io.unlock=packet_unlock;packets.io.now_ns=packet_now;packets.io.delay_us=packet_delay;
 worker=nullptr;last_loop=nullptr;allocated=fail_allocate=add_calls=fail_add=fail_remove=fail_timer=hold_busy=hold_fault=put_fail=rx_delivered=close_in_callback=0;interrupts=true;sequence=0;
}
static void release_worker()
{assert(!i9100_bcm_worker_destroy(&worker)&&!worker&&host_holds==1U&&!live_objects);close_boot();}
static struct i9100_bcm_worker_status status()
{struct i9100_bcm_worker_status s;assert(!i9100_bcm_worker_status(worker,&s));return s;}
static uint8_t *frame(unsigned n,unsigned channel)
{
 unsigned rest=n>32U?n-32U:0U;rest=rest>512U?(rest+511U)&~511U:(rest+3U)&~3U;unsigned wire=32U+rest;
 assert(rx_cursor==0&&rx_bytes+wire<=sizeof(rx));uint8_t *p=rx+rx_bytes;memset(p,0,wire);rx_bytes+=wire;
 put16(p,(uint16_t)n);put16(p+2,(uint16_t)~n);p[4]=sequence++;p[5]=(uint8_t)channel;p[7]=12;p[9]=(uint8_t)(bus.tx_sequence+32U);m.sdio_regs[0x20U/4U]|=0x40U;return p;
}
static void clear_rx(){assert(rx_cursor==rx_bytes);rx_cursor=rx_bytes=0;}
static void eof(){assert(rx_bytes+32U<=sizeof(rx));memset(rx+rx_bytes,0,32);rx_bytes+=32;}
static void event(unsigned type,unsigned flags)
{uint8_t *p=frame(88,1);p[12]=0x20;uint8_t *v=p+16;v[12]=0x88;v[13]=0x6c;v[14]=0x80;v[15]=1;v[17]=58;v[20]=0x10;v[21]=0x18;v[23]=1;v[25]=2;v[27]=(uint8_t)flags;v[31]=(uint8_t)type;memcpy(v+48,"\x02\x01\x02\x03\x04\x05",6);memcpy(v+54,"wlan0",5);}
static void ack()
{
 clear_rx();uint32_t command=bus.request_command;unsigned bytes=command==262U?16U:command==23U?6U:command==25U?36U:0U;
 uint8_t *p=frame(28U+bytes,0);put32(p+12,command);put32(p+16,bytes);put32(p+20,(uint32_t)bus.request_id<<16);
 if(command==23U)memcpy(p+28,"\x02\x01\x02\x03\x04\x05",6);
 if(command==25U){put32(p+28,6);memcpy(p+32,"S2-lab",6);}eof();last_loop->tick();assert(rx_bytes==rx_cursor);
}
static void linked()
{
 start_host();assert(!i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker)&&worker&&host_holds==2U);assert(!i9100_bcm_worker_start(worker));
 i9100_wlan_config c={};c.ssid_bytes=6;memcpy(c.ssid,"S2-lab",6);memcpy(c.bssid,"\x02\x01\x02\x03\x04\x05",6);
 assert(!i9100_bcm_worker_join(worker,&c));last_loop->pump();assert(bus.pending);
 for(unsigned n=0;n<12U&&status().control_state==I9100_WLAN_CONFIGURING;n++){if(bus.pending)ack();else last_loop->tick();}
 assert(status().control_state==I9100_WLAN_JOINING&&!bus.pending);clear_rx();event(0,0);event(16,1);eof();last_loop->tick();
 assert(bus.pending&&bus.request_command==23U);ack();assert(bus.request_command==25U);ack();assert(status().control_state==I9100_WLAN_UP);
}
static void notifications_and_delivery()
{
 linked();i9100_bcm_notify n={};assert(!i9100_bcm_worker_notifier_get(worker,&n));
 unsigned before=rx_wire_calls;interrupts=false;assert(!i9100_bcm_worker_signal(&n)&&rx_wire_calls==before);interrupts=true;
 assert(status().notifier_wakes==1U&&status().pending&&status().notifiers==1U);
 last_loop->pump();assert(!status().pending&&rx_wire_calls==before);
 assert(i9100_bcm_worker_destroy(&worker)==I9100_PUMP_BUSY&&worker&&status().running);
 i9100_bcm_notify copy=n;assert(i9100_bcm_worker_notifier_put(&copy)==I9100_PUMP_ARGUMENT);
 assert(!i9100_bcm_worker_notifier_put(&n));assert(i9100_bcm_worker_signal(&n)==I9100_PUMP_ARGUMENT);
 clear_rx();uint8_t *p=frame(80,2);p[12]=0x20;p[30]=0x5a;eof();close_in_callback=1;last_loop->tick();assert(rx_delivered==1U&&status().data_frames==1U);
 uint8_t ethernet[64]={};assert(!i9100_bcm_worker_send(worker,ethernet,64,0));
 assert(i9100_bcm_worker_destroy(&worker)==I9100_PUMP_BUSY&&worker&&status().running);
 assert(!i9100_bcm_worker_disconnect(worker));last_loop->pump();assert(bus.request_command==52U);ack();clear_rx();event(16,0);eof();last_loop->tick();ack();
 assert(status().control_state==I9100_WLAN_IDLE);release_worker();
}
static void faults_and_cleanup()
{
 linked();m.sdio_regs[0x20U/4U]=0x80U;m.sdio_regs[0x4cU/4U]=0x10U;last_loop->tick();
 assert(status().fault&&!status().running&&!status().ready&&status().error==I9100_PUMP_HALTED);unsigned before=rx_wire_calls;
 last_loop->pump();last_loop->tick();assert(rx_wire_calls==before);release_worker();
 start_host();assert(!i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker));fail_timer=1;
 assert(i9100_bcm_worker_start(worker)==I9100_PUMP_IO&&status().fault);fail_timer=0;release_worker();
 start_host();assert(!i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker));assert(!i9100_bcm_worker_start(worker));hold_busy=1;
 last_loop->tick();m.ns+=500000001ULL;last_loop->tick();assert(status().fault&&status().error==I9100_WLAN_TIMEOUT);hold_busy=0;release_worker();
 start_host();assert(!i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker));assert(!i9100_bcm_worker_start(worker));hold_fault=1;last_loop->tick();
 assert(status().fault&&status().error==I9100_GPIO_IO);hold_fault=0;release_worker();
 start_host();assert(!i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker));hold_fault=1;
 assert(i9100_bcm_worker_start(worker)==I9100_GPIO_IO&&status().fault&&!status().ready&&status().error==I9100_GPIO_IO);hold_fault=0;release_worker();
 linked();hold_fault=1;uint8_t rejected[64]={};unsigned old_tx=tx_bytes;
 assert(i9100_bcm_worker_send(worker,rejected,64,0)==I9100_GPIO_IO&&status().fault&&tx_bytes==old_tx);hold_fault=0;release_worker();
 for(unsigned fail=0;fail<2U;fail++){
  start_host();assert(!i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker));if(fail)put_fail=1;else fail_remove=1;
  assert(i9100_bcm_worker_destroy(&worker)!=0&&worker&&host_holds==2U&&live_objects&&!status().running&&!status().ready);
  put_fail=fail_remove=0;release_worker();
 }
 /* Retained disabled sources cannot invoke a freed owner on a late check. */
 start_host();assert(!i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker));assert(!i9100_bcm_worker_start(worker));
 IOEventSource *old_event=last_loop->sources[0];auto *old_timer=dynamic_cast<IOTimerEventSource *>(last_loop->sources[1]);old_event->retain();old_timer->retain();
 assert(!i9100_bcm_worker_destroy(&worker)&&!worker&&host_holds==1U&&live_objects==2U);
 assert(!old_event->checkForWork());old_timer->queued=true;old_timer->fire();old_event->release();old_timer->release();assert(!live_objects);close_boot();
}
static void construction_failures()
{
 start_host();unsigned before=allocated;assert(i9100_bcm_worker_create(0,&boot,&session,1000,&output,&worker)==I9100_PUMP_DISABLED&&!worker&&allocated==before);close_boot();
 for(unsigned point=1;point<=7U;point++){
  start_host();fail_allocate=point;assert(i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker)!=0&&!worker&&!live_objects&&host_holds==1U);close_boot();
 }
 for(unsigned point=1;point<=2U;point++){
  start_host();fail_add=point;assert(i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker)!=0&&!worker&&!live_objects&&host_holds==1U);close_boot();
 }
 start_host();fail_allocate=5;put_fail=1;
 assert(i9100_bcm_worker_create(1,&boot,&session,1000,&output,&worker)==I9100_PUMP_IO&&worker&&!last_loop&&host_holds==2U);
 assert(!status().ready&&status().worker_hold&&status().cleanup_error==I9100_GPIO_BUSY);
 put_fail=0;release_worker();
}
int main()
{
 notifications_and_delivery();faults_and_cleanup();construction_failures();
 puts("PASS real OSObject/IOWorkLoop/worker+F1/F2/CDC/controller: notifier deferred no SDIO/retain+close gate/callback destroy reject/TX+RX/disconnect before release/HALT preserves cause/native fault+bounded busy/all allocations+add failures/remove+put retention/disabled late sources; host/native-hold/card/FW stand-ins, no physical IRQ or native enumeration proof");return 0;
}
