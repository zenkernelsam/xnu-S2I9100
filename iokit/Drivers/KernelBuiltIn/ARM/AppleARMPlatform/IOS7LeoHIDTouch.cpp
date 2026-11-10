/* HTC HD2 integration/publication changes: Garysss123, 2026-10-04. Original license notices are preserved. */
#if defined(BOARD_CONFIG_QSD8250_LEO) || defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
/* Real Type2A controller reports through the original iOS7 IOHIDLib queue.
 * PL050, synthetic contacts, buttons and userland routing helpers are absent. */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#ifdef enqueue
#undef enqueue
#endif
#include <IOKit/IOSharedDataQueue.h>
#include <IOKit/IODataQueueShared.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include "IOS7LeoTouchTransport.h"
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include "i9100_touch.h"
#define TOUCH_LOG_PREFIX "I9100 TOUCH"
#else
#define TOUCH_LOG_PREFIX "IOS7LEO TOUCH"
#endif
#include "IOS7LeoTouchWire.h"
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include "i9100_home.h"
#include "IOS6I9100HIDWire.h"
#include "i9100_home_observation.h"
#include "I9100HomeTest.h"
#include "i9100_touch_observation.h"
#include <pexpert/pexpert.h>
#endif
#include "IOS7LeoTouchObservation.h"
#include "IOS7LeoTouchQueueProtocol.h"
extern "C" {
#include <mach/mach_time.h>
extern int proc_selfpid(void);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
extern void _disable_preemption(void);
extern void _enable_preemption(void);
#endif
}
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
/* Input workloop owns the writes; panel diagnostics read RAM only. */
volatile I9100HomeObservation i9100_home_observation;
volatile I9100TouchObservation i9100_touch_observation;
volatile uint32_t i9100_home_test_diag[6];
#endif
static volatile UInt32 leo_touch_observer_guard,leo_touch_observer_loss,leo_touch_observer_unknown;
static LeoTouchObservation leo_touch_observer_state;
static void leo_touch_observer_lost(void)
{
 for(unsigned n=0;n<4;n++){
  UInt32 old=leo_touch_observer_loss;if(old==UINT32_MAX)return;
  if(OSCompareAndSwap(old,old+1U,&leo_touch_observer_loss))return;
 }
 (void)OSCompareAndSwap(0,1,&leo_touch_observer_unknown);
}
extern "C" uint32_t ios7leo_touch_observe(LeoTouchObservation *out)
{
 if(!out || out->version!=IOS7_LEO_TOUCH_OBSERVATION_VERSION)return 0;
 if(!OSCompareAndSwap(0,1,&leo_touch_observer_guard)){leo_touch_observer_lost();return 0;}
 bcopy(&leo_touch_observer_state,out,sizeof(*out));out->version=IOS7_LEO_TOUCH_OBSERVATION_VERSION;
 out->loss=leo_touch_observer_unknown?UINT32_MAX:leo_touch_observer_loss;
 OSMemoryBarrier();leo_touch_observer_guard=0;return out->known?1U:0U;
}
static void leo_touch_inc(unsigned *value) {if(*value!=UINT32_MAX)++*value;}
/* HD2 is ARMv7. Keep the compiler clobber explicit rather than depending on
 * the legacy OSAtomic.h _ARM_ARCH_7 selection and its older CP15 variant. */
static void leo_touch_queue_barrier(void *) {__asm__ volatile("dmb sy" ::: "memory");}
static void leo_touch_queue_copy(void *,void *destination,const void *source,uint32_t bytes)
{memcpy(destination,source,bytes);}

class IOS7LeoHIDQueue : public IOSharedDataQueue {
 OSDeclareDefaultStructors(IOS7LeoHIDQueue)
 unsigned notifyCalls,notifyOK,notifyBusy,notifyErrors,notifyNoPort,lastNotifyResult,lastNotifyTail,lastEnqueueFlags;
 bool lastNotifyValid;
public:
 virtual Boolean initWithCapacity(UInt32 size) {
  notifyCalls=notifyOK=notifyBusy=notifyErrors=notifyNoPort=lastNotifyTail=lastEnqueueFlags=0;
  lastNotifyResult=UINT32_MAX;lastNotifyValid=false;return IOSharedDataQueue::initWithCapacity(size);
 }
 UInt32 readerHead(void) const {return dataQueue?dataQueue->head:0;}
 UInt32 writerTail(void) const {return dataQueue?dataQueue->tail:0;}
 UInt32 enqueueNotificationFlags(void) const {return lastEnqueueFlags;}
 bool portPresent(void) const {return notifyMsg && ((mach_msg_header_t *)notifyMsg)->msgh_remote_port!=MACH_PORT_NULL;}
 bool sane(void) const {
  return dataQueue && dataQueue->queueSize==4096 &&
   dataQueue->head<=4096 && dataQueue->tail<=4096 &&
   !(dataQueue->head&3U) && !(dataQueue->tail&3U);
 }
 /* Only while the exclusive consumer is opening/closing under service lock. */
 void discardAll(void) {if(dataQueue){dataQueue->head=dataQueue->tail=0;OSMemoryBarrier();}}
 virtual Boolean enqueue(void *data,UInt32 bytes) {
  lastEnqueueFlags=0;if(!dataQueue)return false;
  LeoTouchQueueHooks hooks={0,leo_touch_queue_barrier,leo_touch_queue_copy};
  uint32_t flags=0;
  if(!leo_touch_queue_enqueue(&dataQueue->head,&dataQueue->tail,dataQueue->queueSize,
   (unsigned char *)dataQueue->queue,data,bytes,&hooks,&flags))return false;
  lastEnqueueFlags=flags;
  if(flags&LEO_TOUCH_UP_NOTIFY_SELECTED)sendDataAvailableNotification();
  return true;
 }
 virtual void sendDataAvailableNotification(void) {
  leo_touch_inc(&notifyCalls);lastNotifyTail=writerTail();lastNotifyResult=UINT32_MAX;lastNotifyValid=false;
  mach_msg_header_t *message=(mach_msg_header_t *)notifyMsg;
  if(!message || !message->msgh_remote_port){leo_touch_inc(&notifyNoPort);return;}
  lastEnqueueFlags|=LEO_TOUCH_UP_NOTIFY_PORT;
  kern_return_t result=mach_msg_send_from_kernel_proper(message,message->msgh_size);
  lastNotifyResult=(UInt32)result;lastNotifyValid=true;
  if(result==MACH_MSG_SUCCESS){leo_touch_inc(&notifyOK);lastEnqueueFlags|=LEO_TOUCH_UP_NOTIFY_OK;}
  else if(result==MACH_SEND_TIMED_OUT){leo_touch_inc(&notifyBusy);lastEnqueueFlags|=LEO_TOUCH_UP_NOTIFY_BUSY;}
  else{leo_touch_inc(&notifyErrors);lastEnqueueFlags|=LEO_TOUCH_UP_NOTIFY_ERROR;}
 }
 void snapshot(LeoTouchObservation *out) const {
  out->notify_calls=notifyCalls;out->notify_ok=notifyOK;out->notify_busy=notifyBusy;
  out->notify_errors=notifyErrors;out->notify_no_port=notifyNoPort;
  out->last_notify_result=lastNotifyResult;out->last_notify_tail=lastNotifyTail;
  if(portPresent())out->observation_flags|=LEO_TOUCH_OBS_PORT_PRESENT;
  if(lastNotifyValid)out->observation_flags|=LEO_TOUCH_OBS_NOTIFY_RESULT_VALID;
  if(notifyCalls==UINT32_MAX || notifyOK==UINT32_MAX || notifyBusy==UINT32_MAX ||
   notifyErrors==UINT32_MAX || notifyNoPort==UINT32_MAX)out->observation_flags|=LEO_TOUCH_OBS_COUNTER_SATURATED;
 }
};
class IOS7LeoHIDTouchClient;
/* Matching class name is the original userspace IOServiceMatching contract. */
class IOHIDEventService : public IOService {
 OSDeclareDefaultStructors(IOHIDEventService)
 IOMemoryDescriptor *device[LEO_TOUCH_REGIONS],*queueMemory;
 IOMemoryMap *mapping[LEO_TOUCH_REGIONS];
 volatile uint8_t *registers[LEO_TOUCH_REGIONS];
 IOLock *lock;
 IOWorkLoop *loop;
 IOTimerEventSource *timer;
 IOS7LeoHIDQueue *queue;
 IOS7LeoHIDTouchClient *consumer;
 LeoTouchState state;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 bool homeDevice,ios6Wire;
 uint32_t homeSavedCon,homeSavedPull;
 I9100HomeState homeState;
 I9100HomeTest homeTest;
 I9100HomePacket lastHomePacket;
 bool lastHomeValid;
 void pollHome(void);
 i9100_touch_bus touchBus;
 i9100_touch_state touchState;
 void updateI9100State(void);
#endif
 LeoHIDPacket pendingPacket,lastPacket;
 bool opened,pending,previousDown,lastValid;
 unsigned packetLogs,eventLogs,queueErrors,transferLogs,readerProgress,readerLogs,observedHead;
 unsigned initValid,published,clientPID,mapCalls,notificationCalls,enqueued,downEvents,moveEvents,upEvents,queueFailures;
 unsigned queueEpoch,lastUpEpoch,lastUpEnqueued,lastUpTail,lastUpSequence,lastUpX,lastUpY,lastUpNotifyFlags;
 uint64_t lastUpTimestamp,serviceSender;
 int initResult;
 static uint32_t readRegister(void *,uint32_t,uint32_t);
 static void writeRegister(void *,uint32_t,uint32_t,uint32_t);
 static uint64_t nowNanoseconds(void *);
 static int delayMicroseconds(void *,uint32_t);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 static void beginTouchTransfer(void *);
 static void endTouchTransfer(void *);
#endif
 static void tick(OSObject *,IOTimerEventSource *);
 void poll(void);
 bool publishProperties(void);
 void commitPacket(void);
 bool enqueuePacket(void);
 void closeReader(void);
 void publishSnapshot(void);
public:
 virtual bool init(OSDictionary *dictionary=0);
 virtual bool start(IOService *provider);
 virtual void free(void);
 virtual IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **);
 bool claim(IOS7LeoHIDTouchClient *);
 void unclaim(IOS7LeoHIDTouchClient *);
 IOReturn method(uint32_t,IOExternalMethodArguments *);
 IOReturn setPort(IOS7LeoHIDTouchClient *,mach_port_t,UInt32);
 IOReturn mapQueue(UInt32,IOOptionBits *,IOMemoryDescriptor **);
 void notePublished(void);
};
class IOS7LeoHIDTouchClient : public IOUserClient {
 OSDeclareDefaultStructors(IOS7LeoHIDTouchClient)
 IOHIDEventService *service;
 mach_port_t notification;
 IOLock *clientLock;
public:
 virtual bool initWithTask(task_t,void *,UInt32,OSDictionary *);
 virtual bool start(IOService *);
 virtual void free(void);
 virtual IOReturn clientClose(void);
 virtual IOService *getService(void);
 /* Both original ABI slots exist in this XNU. MIG dispatch uses the64-bit slot. */
 virtual IOReturn registerNotificationPort(mach_port_t,UInt32,io_user_reference_t);
 virtual IOReturn registerNotificationPort(mach_port_t,UInt32,UInt32);
 virtual IOReturn clientMemoryForType(UInt32,IOOptionBits *,IOMemoryDescriptor **);
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,
  IOExternalMethodDispatch *,OSObject *,void *);
};
OSDefineMetaClassAndStructors(IOS7LeoHIDQueue,IOSharedDataQueue)
OSDefineMetaClassAndStructors(IOHIDEventService,IOService)
OSDefineMetaClassAndStructors(IOS7LeoHIDTouchClient,IOUserClient)

bool IOHIDEventService::init(OSDictionary *dictionary)
{
 if(!IOService::init(dictionary))return false;
 bzero(device,sizeof(device));bzero(mapping,sizeof(mapping));bzero(registers,sizeof(registers));
 queueMemory=0;lock=0;loop=0;timer=0;queue=0;consumer=0;
 bzero(&state,sizeof(state));
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 homeDevice=getProperty("I9100HomeButton")!=0;homeSavedCon=homeSavedPull=0;
 int wireABI=0;PE_parse_boot_argn("i9100hidabi",&wireABI,sizeof(wireABI));ios6Wire=wireABI==6;
 i9100_home_reset(&homeState);bzero(&lastHomePacket,sizeof(lastHomePacket));lastHomeValid=false;
 bzero(&homeTest,sizeof(homeTest));
 if(homeDevice){int diagnostic=0,control=0,enabled=0;
  PE_parse_boot_argn("i9100hometest",&enabled,sizeof(enabled));
  PE_parse_boot_argn("i9100control",&control,sizeof(control));
  PE_parse_boot_argn("i9100xpcdiag",&diagnostic,sizeof(diagnostic));
  homeTest.enabled=enabled==1 && control==1 && diagnostic==1;
 }
 bzero(&touchBus,sizeof(touchBus));bzero(&touchState,sizeof(touchState));
#endif
 bzero(&pendingPacket,sizeof(pendingPacket));bzero(&lastPacket,sizeof(lastPacket));
 opened=pending=previousDown=lastValid=false;
 packetLogs=eventLogs=queueErrors=transferLogs=readerProgress=readerLogs=observedHead=0;
 initValid=published=clientPID=mapCalls=notificationCalls=enqueued=downEvents=moveEvents=upEvents=queueFailures=0;
 queueEpoch=lastUpEpoch=lastUpEnqueued=lastUpTail=lastUpSequence=lastUpX=lastUpY=lastUpNotifyFlags=0;
 /* IORegistryEntry assigns its ID on attachToParent, after init. */
 lastUpTimestamp=0;serviceSender=0;
 initResult=LEO_TOUCH_NOT_READY;
 return true;
}
void IOHIDEventService::publishSnapshot(void)
{
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(homeDevice){
  i9100_home_observation.opened=opened;
  i9100_home_observation.head=queue?queue->readerHead():0;
  i9100_home_observation.tail=queue?queue->writerTail():0;
  return;
 }
 i9100_touch_observation.ready=touchState.ready;
 i9100_touch_observation.init_result=(uint32_t)initResult;
 i9100_touch_observation.stage=touchState.stage;
 i9100_touch_observation.opened=opened;
 i9100_touch_observation.polls=touchState.polls;
 i9100_touch_observation.reports=touchState.packets;
 i9100_touch_observation.enqueued=enqueued;
 i9100_touch_observation.head=queue?queue->readerHead():0;
 i9100_touch_observation.tail=queue?queue->writerTail():0;
 i9100_touch_observation.errors=touchState.errors;
#endif
 if(!OSCompareAndSwap(0,1,&leo_touch_observer_guard)){leo_touch_observer_lost();return;}
 serviceSender=getRegistryEntryID();
 LeoTouchObservation v={};v.version=IOS7_LEO_TOUCH_OBSERVATION_VERSION;v.known=1;
 v.init_valid=initValid;v.init_ready=state.ready;v.init_result=initResult;
 v.init_stage=state.stage;v.id_word=state.id_word;v.published=published;
 v.client_pid=clientPID;v.opened=opened;v.map_calls=mapCalls;v.notification_calls=notificationCalls;
 v.real_reports=state.packets;v.enqueued=enqueued;v.down_events=downEvents;v.move_events=moveEvents;v.up_events=upEvents;
 v.queue_failures=queueFailures;v.reader_progress=readerProgress;
 v.reader_head=queue?queue->readerHead():0;v.writer_tail=queue?queue->writerTail():0;v.pending=pending;
 v.last_sequence=state.sequence;v.last_x=state.last_x;v.last_y=state.last_y;v.last_down=(state.last_contacts!=0);
 v.transport_errors=state.errors;
 v.queue_epoch=queueEpoch;v.last_up_epoch=lastUpEpoch;v.last_up_enqueued=lastUpEnqueued;
 v.last_up_tail=lastUpTail;v.last_up_sequence=lastUpSequence;v.last_up_x=lastUpX;v.last_up_y=lastUpY;
 v.last_up_notify_flags=lastUpNotifyFlags;v.last_up_timestamp=lastUpTimestamp;v.service_sender=serviceSender;
 v.last_notify_result=UINT32_MAX;if(queue)queue->snapshot(&v);
 if(queueEpoch==UINT32_MAX || enqueued==UINT32_MAX || downEvents==UINT32_MAX || moveEvents==UINT32_MAX ||
  upEvents==UINT32_MAX || queueFailures==UINT32_MAX || readerProgress==UINT32_MAX || mapCalls==UINT32_MAX ||
  notificationCalls==UINT32_MAX || state.packets==UINT32_MAX || state.sequence==UINT32_MAX || state.errors==UINT32_MAX)
  v.observation_flags|=LEO_TOUCH_OBS_COUNTER_SATURATED;
 bcopy(&v,&leo_touch_observer_state,sizeof(v));
 OSMemoryBarrier();leo_touch_observer_guard=0;
}
void IOHIDEventService::notePublished(void)
{
 IOLockLock(lock);published=1;publishSnapshot();IOLockUnlock(lock);
}
uint32_t IOHIDEventService::readRegister(void *cookie,uint32_t region,uint32_t offset)
{
 IOHIDEventService *s=(IOHIDEventService *)cookie;
 if(!s || region>=LEO_TOUCH_REGIONS || offset>=4096 || (offset&3U) || !s->registers[region])return UINT32_MAX;
 __asm__ volatile("dsb sy" ::: "memory");
 uint32_t value=*(volatile uint32_t *)(s->registers[region]+offset);
 __asm__ volatile("dsb sy" ::: "memory");return value;
}
void IOHIDEventService::writeRegister(void *cookie,uint32_t region,uint32_t offset,uint32_t value)
{
 IOHIDEventService *s=(IOHIDEventService *)cookie;
 if(!s || region>=LEO_TOUCH_REGIONS || offset>=4096 || (offset&3U) || !s->registers[region])return;
 *(volatile uint32_t *)(s->registers[region]+offset)=value;
 __asm__ volatile("dsb sy" ::: "memory");
}
uint64_t IOHIDEventService::nowNanoseconds(void *)
{
 uint64_t now=0;(absolutetime_to_nanoseconds)(mach_absolute_time(),&now);return now;
}
int IOHIDEventService::delayMicroseconds(void *,uint32_t microseconds)
{
 if(microseconds>1000U)IOSleep(microseconds/1000U+(microseconds%1000U!=0));
 else IODelay(microseconds);
 return 1;
}
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
void IOHIDEventService::beginTouchTransfer(void *) { _disable_preemption(); }
void IOHIDEventService::endTouchTransfer(void *) { _enable_preemption(); }
void IOHIDEventService::updateI9100State(void)
{
 state.ready=touchState.ready;state.stage=touchState.stage;state.id_word=touchState.id_word;
 state.last_result=touchState.last_result;state.last_status=touchState.last_status;
 state.last_diagnostic=(touchBus.last_phase<<16)|((uint32_t)touchBus.last_error&0xffffU);
 state.packets=touchState.packets;state.sequence=touchState.sequence;state.errors=touchState.errors;
 state.last_x=touchState.last_x;state.last_y=touchState.last_y;
 /* Observation tracks the emitted primary contact, not an ignored second finger. */
 state.last_contacts=touchState.primary?1U:0U;
}
#endif
bool IOHIDEventService::publishProperties(void)
{
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(homeDevice){
  setName("IOS7I9100HomeButton");setProperty("HIDServiceSupport",true);
  setProperty("PrimaryUsagePage",12,32);setProperty("PrimaryUsage",1,32);
  setProperty("Built-In",true);setProperty("Transport","GPIO");
  setProperty("Product","Samsung Galaxy S2 Home button");
  setProperty("IOS7LabBackend","GT-I9100 GPX3(5) active-low Consumer Menu");
 }else{
 setName("IOS7I9100Touch");setProperty("HIDServiceSupport",true);
 setProperty("IOS7LabBackend","GT-I9100 physical Atmel maXTouch native HID queue");
 /* backboardd looks for the multitouch gesture source as
  * {PrimaryUsage:4, HIDServiceSupport, PrimaryUsagePage:0xff00} — the
  * Apple vendor-defined usage page, not generic Digitizers(0x0d). */
 setProperty("PrimaryUsagePage",0xff00,32);setProperty("PrimaryUsage",4,32);
 setProperty("Built-In",true);setProperty("DisplayIntegrated",true);
 setProperty("Transport","I2C");setProperty("Product","GT-I9100 Atmel mXT224/224E");
 setProperty("TouchTransportReady",touchState.ready!=0);
 setProperty("TouchControllerID",touchState.id_word,32);
 setProperty("TouchInitResult",(uint32_t)initResult,32);
 setProperty("TouchRawMaximumX",touchState.x_max,32);setProperty("TouchRawMaximumY",touchState.y_max,32);
 }
#elif defined(BOARD_CONFIG_EXYNOS3475_J1XLTE)
 setName("IOS7J1Touch");setProperty("HIDServiceSupport",true);
 setProperty("IOS7LabBackend","Samsung J1 touch stub native HID queue (no-input)");
 setProperty("PrimaryUsagePage",13,32);setProperty("PrimaryUsage",4,32);
 setProperty("Built-In",true);setProperty("DisplayIntegrated",true);
 setProperty("Transport","I2C");setProperty("Product","J1 touch stub");
#else
 setName("IOS7LeoType2ATouch");setProperty("HIDServiceSupport",true);
 setProperty("IOS7LabBackend","HTC Leo physical Type2A I2C native HID queue");
 setProperty("PrimaryUsagePage",13,32);setProperty("PrimaryUsage",4,32);
 setProperty("Built-In",true);setProperty("DisplayIntegrated",true);
 setProperty("Transport","I2C");setProperty("Product","HTC Leo Type2A touch");
#endif
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 setProperty("Manufacturer","Samsung / Atmel");
#else
 setProperty("Manufacturer","HD2 legacy lab");
#endif
 setProperty("VendorID",(unsigned long long)0,32);
 setProperty("ProductID",1,32);setProperty("ReportInterval",20000,32);setProperty("QueueSize",4096,32);
 OSDictionary *pair=OSDictionary::withCapacity(2);OSArray *pairs=OSArray::withCapacity(1);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 OSNumber *page=OSNumber::withNumber(homeDevice?12:13,32),*usage=OSNumber::withNumber(homeDevice?1:4,32);
#else
 OSNumber *page=OSNumber::withNumber(13,32),*usage=OSNumber::withNumber(4,32);
#endif
 if(!pair || !pairs || !page || !usage){if(pair)pair->release();if(pairs)pairs->release();if(page)page->release();if(usage)usage->release();return false;}
 pair->setObject("DeviceUsagePage",page);pair->setObject("DeviceUsage",usage);
 pairs->setObject(pair);setProperty("DeviceUsagePairs",pairs);
 page->release();usage->release();pair->release();pairs->release();
 OSDictionary *plugins=OSDictionary::withCapacity(4);
 OSString *path=OSString::withCString("IOHIDFamily.kext/PlugIns/IOHIDLib.plugin");
 if(!plugins || !path){if(plugins)plugins->release();if(path)path->release();return false;}
 plugins->setObject("7DDEECA8-A7B4-11DA-8A0E-0014519758EF",path);
 plugins->setObject("FA12FA38-6F1A-11D4-BA0C-0005028F18D5",path);
 plugins->setObject("0516B563-B15B-11DA-96EB-0014519758EF",path);
 plugins->setObject("40A57A4E-26A0-11D8-9295-000A958A2C78",path);
 setProperty("IOCFPlugInTypes",plugins);path->release();plugins->release();return true;
}
bool IOHIDEventService::start(IOService *provider)
{
 if(!IOService::start(provider))return false;
 lock=IOLockAlloc();queue=new IOS7LeoHIDQueue;
 if(!lock || !queue || !queue->initWithCapacity(4096))return false;
 queueMemory=queue->getMemoryDescriptor();if(!queueMemory)return false;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(homeDevice){
  device[1]=IOMemoryDescriptor::withPhysicalAddress(I9100_TOUCH_GPIO2_PA,4096,kIODirectionInOut);
  mapping[1]=device[1]?device[1]->map(kIOMapInhibitCache):0;
  if(!mapping[1] || !mapping[1]->getAddress())return false;
  registers[1]=(volatile uint8_t *)mapping[1]->getAddress();
  homeSavedCon=readRegister(this,1,I9100_HOME_CON);homeSavedPull=readRegister(this,1,I9100_HOME_PULL);
  writeRegister(this,1,I9100_HOME_CON,i9100_home_input_con(homeSavedCon));
  writeRegister(this,1,I9100_HOME_PULL,i9100_home_input_pull(homeSavedPull));
  i9100_home_observation.enabled=1;
  loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,tick);
  if(!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess || !publishProperties())return false;
  if(timer->setTimeoutMS(20)!=kIOReturnSuccess)return false;
  IOLog("I9100 HOME GPX3(5) active-low Consumer 0c/40 native52B queue4096\n");
  return true;
 }
 static const uint32_t physical[2]={I9100_TOUCH_GPIO1_PA,I9100_TOUCH_GPIO2_PA};
 for(unsigned n=0;n<2;n++){
  device[n]=IOMemoryDescriptor::withPhysicalAddress(physical[n],4096,kIODirectionInOut);
  mapping[n]=device[n]?device[n]->map(kIOMapInhibitCache):0;
  if(!mapping[n] || !mapping[n]->getAddress()){
   IOLog("I9100 TOUCH GPIO mapping failed region=%u\n",n);return false;
  }
  registers[n]=(volatile uint8_t *)mapping[n]->getAddress();
 }
 i9100_touch_gpio_io gpioIO={this,readRegister,writeRegister,delayMicroseconds,1000,beginTouchTransfer,endTouchTransfer};
 int result=i9100_touch_bus_init(&touchBus,&gpioIO);
 if(!result){
  i9100_touch_io io={&touchBus,i9100_touch_bus_read,i9100_touch_bus_write,i9100_touch_bus_changed};
  result=i9100_touch_init(&touchState,&io);
 }
 initValid=1;initResult=result;updateI9100State();publishSnapshot();
 IOLog("I9100 TOUCH init=%d stage=%u id=%08x CHG=%u bus=%d T5=%04x len=%u T9=%04x reports=%u..%u\n",
  result,touchState.stage,touchState.id_word,(unsigned)((readRegister(this,1,0xc04)>>4)&1U),touchBus.last_error,
  (unsigned)touchState.message_address,(unsigned)touchState.message_size,(unsigned)touchState.touch_address,
  (unsigned)touchState.first_report,(unsigned)(touchState.report_count?touchState.first_report+touchState.report_count-1U:0));
 IOLog("I9100 TOUCH range=%ux%u orient=%u drop=%u,%u; physical480x800\n",
  (unsigned)touchState.x_max,(unsigned)touchState.y_max,(unsigned)touchState.orientation,
  (unsigned)touchState.x_drop,(unsigned)touchState.y_drop);
 /* Retain the existing HID publication if hardware probing fails, so boot
  * remains usable for diagnostics. Ready stays false; no input is invented. */
 loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,tick);
 if(!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess)return false;
 if(!publishProperties())return false;
 if(timer->setTimeoutMS(20)!=kIOReturnSuccess)return false;
 IOLog("I9100 TOUCH service ready transport=%u primary-finger queue4096\n",touchState.ready);
 return true;
#elif defined(BOARD_CONFIG_EXYNOS3475_J1XLTE)
 /* J1: no touch controller mapped yet; publish HID service with no-input
  * transport so SpringBoard GUI shows. Swipe wired later. */
 state.ready=1; state.stage=7;
 initValid=1; initResult=0; publishSnapshot();
 loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,tick);
 if(!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess)return false;
 if(!publishProperties())return false;
 if(timer->setTimeoutMS(20)!=kIOReturnSuccess)return false;
 IOLog("IOS7J1 TOUCH service-ready stub physical480x800 queue4096; no-input\n");
 return true;
#else
 static const uint32_t physical[LEO_TOUCH_REGIONS]={0xa9900000U,0xa8600000U,0xa9000000U,0xa9100000U};
 for(unsigned n=0;n<LEO_TOUCH_REGIONS;n++){
  device[n]=IOMemoryDescriptor::withPhysicalAddress(physical[n],4096,kIODirectionInOut);
  mapping[n]=device[n]?device[n]->map(kIOMapInhibitCache):0;
  if(!mapping[n] || !mapping[n]->getAddress()){
   IOLog(TOUCH_LOG_PREFIX " mapping-failed region=%u; no service published\n",n);return false;
  }
  registers[n]=(volatile uint8_t *)mapping[n]->getAddress();
 }
 LeoTouchIO io={this,readRegister,writeRegister,nowNanoseconds,delayMicroseconds,4096};
 int result=leo_touch_transport_init(&state,&io);
 initValid=1;initResult=result;publishSnapshot();
 IOLog(TOUCH_LOG_PREFIX " init result=%d stage=%u id=%08x diagnostic=%08x status=%08x actual-controller-only\n",
  result,state.stage,state.id_word,state.last_diagnostic,state.last_status);
 if(result!=LEO_TOUCH_OK || !state.ready)return false;
 loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,tick);
 if(!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess)return false;
 if(!publishProperties())return false;
 if(timer->setTimeoutMS(20)!=kIOReturnSuccess)return false;
 IOLog(TOUCH_LOG_PREFIX " service-ready usage13/4 physical480x800 native204B queue4096; not-gesture-proof\n");
 return true;
#endif
}
void IOHIDEventService::free(void)
{
 if(timer)timer->cancelTimeout();if(loop && timer)loop->removeEventSource(timer);
 if(timer)timer->release();if(loop)loop->release();
 if(queueMemory)queueMemory->release();if(queue)queue->release();
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(homeDevice && registers[1]){
  writeRegister(this,1,I9100_HOME_CON,i9100_home_restore_con(readRegister(this,1,I9100_HOME_CON),homeSavedCon));
  writeRegister(this,1,I9100_HOME_PULL,i9100_home_restore_pull(readRegister(this,1,I9100_HOME_PULL),homeSavedPull));
  i9100_home_observation.enabled=0;
 }else i9100_touch_bus_fini(&touchBus);
#endif
 for(unsigned n=0;n<LEO_TOUCH_REGIONS;n++){
  if(mapping[n])mapping[n]->release();if(device[n])device[n]->release();
 }
 if(lock)IOLockFree(lock);IOService::free();
}
void IOHIDEventService::tick(OSObject *owner,IOTimerEventSource *source)
{
 IOHIDEventService *service=OSDynamicCast(IOHIDEventService,owner);
 if(service)service->poll();
 IOReturn result=source->setTimeoutMS(20);
 static unsigned logs;if(result!=kIOReturnSuccess && logs<1U){++logs;IOLog(TOUCH_LOG_PREFIX " rearm-error=%x; no further polling claimed\n",result);}
}
void IOHIDEventService::commitPacket(void)
{
 bool down=(pendingPacket.finger.options&0x20000U)!=0;
 leo_touch_inc(&enqueued);if(down)leo_touch_inc(previousDown?&moveEvents:&downEvents);else leo_touch_inc(&upEvents);
 if(!down){
  lastUpEpoch=queueEpoch;lastUpEnqueued=enqueued;lastUpTail=queue->writerTail();
  lastUpSequence=state.sequence;lastUpX=state.last_x;lastUpY=state.last_y;
  lastUpTimestamp=pendingPacket.header.timeStamp;lastUpNotifyFlags=queue->enqueueNotificationFlags();
 }
 if(eventLogs<32U){
  ++eventLogs;
  IOLog(TOUCH_LOG_PREFIX " enqueued seq=%u kind=%s x=%u y=%u down=%u timestamp=%llu; queue-enqueue-not-UI-consumption\n",
   state.sequence,down?(previousDown?"move":"down"):"up",(unsigned)state.last_x,(unsigned)state.last_y,
   (unsigned)down,(unsigned long long)pendingPacket.header.timeStamp);
 }
 previousDown=down;bcopy(&pendingPacket,&lastPacket,sizeof(lastPacket));lastValid=true;pending=false;
 publishSnapshot();
}
bool IOHIDEventService::enqueuePacket(void)
{
 if(!queue->sane())return false;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(ios6Wire){
  IOS6TouchPacket packet;
  if(!ios6_touch_wire(&pendingPacket,&packet))return false;
  return queue->enqueue(&packet,sizeof(packet));
 }
#endif
 return queue->enqueue(&pendingPacket,sizeof(pendingPacket));
}
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
void IOHIDEventService::pollHome(void)
{
 IOLockLock(lock);
 unsigned down=i9100_home_down(readRegister(this,1,I9100_HOME_DAT));
 unsigned physical=down;uint64_t now=mach_absolute_time();AbsoluteTime delayTime,holdTime;
 nanoseconds_to_absolutetime(5000000000ULL,&delayTime);
 nanoseconds_to_absolutetime(200000000ULL,&holdTime);
 uint64_t delay=__OSAbsoluteTime(delayTime),hold=__OSAbsoluteTime(holdTime);
 down=i9100_home_test_sample(&homeTest,physical,opened,now,delay,hold);
 i9100_home_test_diag[0]=homeTest.enabled;i9100_home_test_diag[1]=homeTest.phase;
 i9100_home_test_diag[2]=physical;i9100_home_test_diag[3]=down;
 i9100_home_test_diag[4]=homeTest.phase==3;i9100_home_test_diag[5]=clientPID;
 i9100_home_observation.down=down;publishSnapshot();
 if(!opened){i9100_home_reset(&homeState);IOLockUnlock(lock);return;}
 unsigned count=homeState.count;
 i9100_home_sample(&homeState,down,now);
 if(homeState.count!=count){
  unsigned edges=i9100_home_observation.edges;
  if(edges!=UINT32_MAX)i9100_home_observation.edges=edges+1U;
 }
 for(unsigned n=0;n<16 && homeState.count;n++){
  I9100HomeEdge *edge=&homeState.edges[homeState.head];I9100HomePacket packet;
  if(!i9100_home_packet(edge->down,edge->timestamp,getRegistryEntryID(),&packet))break;
  if(!queue->sane())break;
  unsigned bytes=sizeof(packet);
  if(ios6Wire){
   IOS6HomePacket wire;
   if(!ios6_home_wire(&packet,&wire) || !queue->enqueue(&wire,sizeof(wire)))break;
   bytes=sizeof(wire);
  }else if(!queue->enqueue(&packet,sizeof(packet)))break;
  bcopy(&packet,&lastHomePacket,sizeof(lastHomePacket));lastHomeValid=true;
  homeState.head=(homeState.head+1U)&15U;homeState.count--;
  unsigned sent=i9100_home_observation.enqueued;
  if(sent!=UINT32_MAX)i9100_home_observation.enqueued=sent+1U;
  if(eventLogs<12){eventLogs++;IOLog("I9100 HOME %s enqueued bytes=%u abi=%u\n",edge->down?"down":"up",bytes,ios6Wire?6U:7U);}
 }
 publishSnapshot();
 IOLockUnlock(lock);
}
#endif
void IOHIDEventService::poll(void)
{
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(homeDevice){pollHome();return;}
#endif
 IOLockLock(lock);
 UInt32 head=queue->readerHead();
 if(head!=observedHead){
  leo_touch_inc(&readerProgress);
  if(readerLogs<8U){++readerLogs;IOLog(TOUCH_LOG_PREFIX " reader-head head=%u tail=%u; queue-progress-only\n",(unsigned)head,(unsigned)queue->writerTail());}
  observedHead=head;
 }
 /* Refresh actual shared RAM frontiers on every normal20ms poll, including
  * no new input/read errors. Observation never initiates hardware reads. */
 publishSnapshot();
 if(pending && opened){
  if(!enqueuePacket()){leo_touch_inc(&queueFailures);publishSnapshot();IOLockUnlock(lock);return;}
  commitPacket();
 }
#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE)
 /* J1 stub: no controller yet, no input, keep service alive for GUI. */
 IOLockUnlock(lock); return;
#else
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 const unsigned batchLimit=16;
#else
 const unsigned batchLimit=1;
#endif
 for(unsigned batch=0;batch<batchLimit;batch++){
 LeoTouchReport report;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(!touchState.ready)break;
 i9100_touch_report rawReport;
 int result=i9100_touch_poll(&touchState,&rawReport);updateI9100State();
 if(result>0){
  bzero(&report,sizeof(report));report.sequence=rawReport.sequence;
  report.x=rawReport.x;report.y=rawReport.y;report.down=rawReport.down;report.contacts=rawReport.contacts;
  memcpy(report.raw,rawReport.raw,sizeof(report.raw));memcpy(state.last_raw,rawReport.raw,sizeof(state.last_raw));
 }
#else
 int result=leo_touch_transport_poll(&state,&report);
#endif
 if(result<0){
  publishSnapshot();
  if(transferLogs<4U){++transferLogs;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
   IOLog("I9100 TOUCH poll-error=%d phase=%u reg=%04x lines=%u power=%u recoveries=%u\n",
    result,touchBus.last_phase,touchBus.last_register,(unsigned)(readRegister(this,0,0x24)&12U),
    (unsigned)((readRegister(this,1,0xc4)>>3)&1U),touchBus.recoveries);
#else
   IOLog(TOUCH_LOG_PREFIX " poll-error=%d stage=%u status=%08x diagnostic=%08x; no-synthetic-up\n",result,state.stage,state.last_status,state.last_diagnostic);
#endif
  }
  IOLockUnlock(lock);return;
 }
 if(result==0){IOLockUnlock(lock);return;}
 publishSnapshot();
 if(packetLogs<32U){
  ++packetLogs;IOLog(TOUCH_LOG_PREFIX " real-report seq=%u x=%u y=%u down=%u contacts=%u raw=%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
   report.sequence,(unsigned)report.x,(unsigned)report.y,(unsigned)report.down,(unsigned)report.contacts,
   report.raw[0],report.raw[1],report.raw[2],report.raw[3],report.raw[4],report.raw[5],report.raw[6],report.raw[7],report.raw[8]);
 }
 if(!opened){previousDown=false;continue;}
 if(!report.down && !previousDown)continue;
 if(!leo_touch_make_hid_packet(report.x,report.y,report.down,previousDown,
  mach_absolute_time(),getRegistryEntryID(),&pendingPacket))continue;
 if(!enqueuePacket()){
  pending=true;
  leo_touch_inc(&queueFailures);publishSnapshot();
  if(queueErrors<4U){++queueErrors;IOLog(TOUCH_LOG_PREFIX " queue-full-or-invalid; retained pending actual contact, no new hardware read\n");}
 }else commitPacket();
 if(pending)break;
 }
 IOLockUnlock(lock);
#endif
}
void IOHIDEventService::closeReader(void)
{
 opened=pending=previousDown=lastValid=false;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(homeDevice){i9100_home_reset(&homeState);lastHomeValid=false;}
#endif
 leo_touch_inc(&queueEpoch);
 queue->discardAll();observedHead=0;
 publishSnapshot();
}
bool IOHIDEventService::claim(IOS7LeoHIDTouchClient *client)
{
 IOLockLock(lock);bool accepted=client && !consumer;if(accepted){consumer=client;clientPID=(unsigned)proc_selfpid();publishSnapshot();}IOLockUnlock(lock);return accepted;
}
void IOHIDEventService::unclaim(IOS7LeoHIDTouchClient *client)
{
 IOLockLock(lock);if(consumer==client){closeReader();consumer=0;clientPID=0;queue->setNotificationPort(MACH_PORT_NULL);publishSnapshot();}IOLockUnlock(lock);
}
IOReturn IOHIDEventService::setPort(IOS7LeoHIDTouchClient *client,mach_port_t port,UInt32 type)
{
 if(type)return kIOReturnUnsupported;
 IOLockLock(lock);
 if(consumer!=client){IOLockUnlock(lock);return kIOReturnNotAttached;}
 queue->setNotificationPort(port);leo_touch_inc(&notificationCalls);publishSnapshot();IOLockUnlock(lock);
 static unsigned logs;if(logs<8U){++logs;IOLog(TOUCH_LOG_PREFIX " notification-port type0 registered; not-event-delivery-proof\n");}
 return kIOReturnSuccess;
}
IOReturn IOHIDEventService::mapQueue(UInt32 type,IOOptionBits *options,IOMemoryDescriptor **memory)
{
 if(type || !options || !memory || !queueMemory)return kIOReturnBadArgument;
 queueMemory->retain();*options=0;*memory=queueMemory;
 IOLockLock(lock);leo_touch_inc(&mapCalls);publishSnapshot();IOLockUnlock(lock);
 static unsigned logs;if(logs<8U){++logs;IOLog(TOUCH_LOG_PREFIX " queue-map type0 bytes=%lu\n",(unsigned long)queueMemory->getLength());}
 return kIOReturnSuccess;
}
IOReturn IOHIDEventService::method(uint32_t selector,IOExternalMethodArguments *args)
{
 if(!args || args->structureInputDescriptor || args->structureOutputDescriptor)return kIOReturnBadArgument;
 static unsigned logs;if(logs<12U){++logs;IOLog(TOUCH_LOG_PREFIX " method=%u sin=%u stin=%u sout=%u stout=%u\n",selector,args->scalarInputCount,args->structureInputSize,args->scalarOutputCount,args->structureOutputSize);}
 if((selector==0 || selector==1) && args->scalarInputCount==1 && args->scalarInput &&
  !args->structureInputSize && !args->scalarOutputCount && !args->structureOutputSize){
  if(args->scalarInput[0])return kIOReturnUnsupported;
  IOLockLock(lock);
  if(selector==0){if(!opened){closeReader();opened=true;}}
  else closeReader();
  publishSnapshot();
  IOLockUnlock(lock);
  static unsigned openLogs;if(openLogs<8U){++openLogs;IOLog(TOUCH_LOG_PREFIX " reader-%s selector=%u\n",selector==0?"open":"close",selector);}
  return kIOReturnSuccess;
 }
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 if(ios6Wire && selector==2 && args->scalarInputCount==2 && args->scalarInput &&
  !args->scalarOutputCount && args->structureOutput){
  if(args->scalarInput[1])return kIOReturnUnsupported;
  if(homeDevice && args->scalarInput[0]==3){
   /* A headset/telephony matching event is not the physical Menu switch. */
   if(!ios6_home_match(args->structureInput,args->structureInputSize))return kIOReturnNotFound;
   if(args->structureOutputSize<sizeof(IOS6HomePacket))return kIOReturnNoSpace;
   I9100HomePacket source;IOS6HomePacket wire;
   IOLockLock(lock);
   bool available=homeState.initialized &&
    i9100_home_packet(homeState.stable,mach_absolute_time(),getRegistryEntryID(),&source) &&
    ios6_home_wire(&source,&wire);
   if(available){bcopy(&wire,args->structureOutput,sizeof(wire));args->structureOutputSize=sizeof(wire);}
   IOLockUnlock(lock);return available?kIOReturnSuccess:kIOReturnNotFound;
  }
  if(!homeDevice && args->scalarInput[0]==11 && !args->structureInputSize){
   if(args->structureOutputSize<sizeof(IOS6TouchPacket))return kIOReturnNoSpace;
   IOS6TouchPacket wire;IOLockLock(lock);bool available=lastValid && ios6_touch_wire(&lastPacket,&wire);
   if(available){bcopy(&wire,args->structureOutput,sizeof(wire));args->structureOutputSize=sizeof(wire);}
   IOLockUnlock(lock);return available?kIOReturnSuccess:kIOReturnNotFound;
  }
  return kIOReturnNotFound;
 }
 if(homeDevice && selector==2 && args->scalarInputCount==2 && args->scalarInput &&
  !args->structureInputSize && !args->scalarOutputCount && args->structureOutput &&
  args->structureOutputSize>=sizeof(lastHomePacket)){
  if(args->scalarInput[0]!=3 || args->scalarInput[1])return kIOReturnUnsupported;
  IOLockLock(lock);bool available=lastHomeValid;
  if(available){memcpy(args->structureOutput,&lastHomePacket,sizeof(lastHomePacket));args->structureOutputSize=sizeof(lastHomePacket);}
  IOLockUnlock(lock);return available?kIOReturnSuccess:kIOReturnNotFound;
 }
#endif
 if(selector==2 && args->scalarInputCount==2 && args->scalarInput && !args->structureInputSize &&
  !args->scalarOutputCount && args->structureOutput && args->structureOutputSize>=sizeof(lastPacket)){
  if(args->scalarInput[0]!=11 || args->scalarInput[1])return kIOReturnUnsupported;
  IOLockLock(lock);bool available=lastValid;
  if(available){memcpy(args->structureOutput,&lastPacket,sizeof(lastPacket));args->structureOutputSize=sizeof(lastPacket);}
  IOLockUnlock(lock);return available?kIOReturnSuccess:kIOReturnNotFound;
 }
 return kIOReturnUnsupported;
}
bool IOS7LeoHIDTouchClient::initWithTask(task_t task,void *security,UInt32 type,OSDictionary *properties)
{
 service=0;notification=MACH_PORT_NULL;clientLock=IOLockAlloc();
 return clientLock && IOUserClient::initWithTask(task,security,type,properties);
}
bool IOS7LeoHIDTouchClient::start(IOService *provider)
{
 if(!IOUserClient::start(provider))return false;
 IOHIDEventService *candidate=OSDynamicCast(IOHIDEventService,provider);
 if(!candidate)return false;
 IOLockLock(clientLock);bool accepted=candidate->claim(this);
 if(accepted)service=candidate;IOLockUnlock(clientLock);return accepted;
}
IOReturn IOS7LeoHIDTouchClient::clientClose(void)
{
 IOLockLock(clientLock);
 if(service){service->unclaim(this);service=0;}
 if(notification){releaseNotificationPort(notification);notification=MACH_PORT_NULL;}
 IOLockUnlock(clientLock);
 terminate();return kIOReturnSuccess;
}
void IOS7LeoHIDTouchClient::free(void)
{
 if(clientLock){
  IOLockLock(clientLock);if(service){service->unclaim(this);service=0;}
  if(notification){releaseNotificationPort(notification);notification=MACH_PORT_NULL;}
  IOLockUnlock(clientLock);IOLockFree(clientLock);clientLock=0;
 }
 IOUserClient::free();
}
IOService *IOS7LeoHIDTouchClient::getService(void)
{
 IOLockLock(clientLock);IOService *result=service;IOLockUnlock(clientLock);return result;
}
IOReturn IOS7LeoHIDTouchClient::registerNotificationPort(mach_port_t port,UInt32 type,io_user_reference_t)
{
 IOLockLock(clientLock);
 if(!service){IOLockUnlock(clientLock);return kIOReturnNotAttached;}
 IOReturn result=service->setPort(this,port,type);
 if(result==kIOReturnSuccess){if(notification)releaseNotificationPort(notification);notification=port;}
 IOLockUnlock(clientLock);
 return result;
}
IOReturn IOS7LeoHIDTouchClient::registerNotificationPort(mach_port_t port,UInt32 type,UInt32 refCon)
{
 return registerNotificationPort(port,type,(io_user_reference_t)refCon);
}
IOReturn IOS7LeoHIDTouchClient::clientMemoryForType(UInt32 type,IOOptionBits *options,IOMemoryDescriptor **memory)
{
 IOLockLock(clientLock);IOReturn result=service?service->mapQueue(type,options,memory):kIOReturnNotAttached;IOLockUnlock(clientLock);return result;
}
IOReturn IOS7LeoHIDTouchClient::externalMethod(uint32_t selector,IOExternalMethodArguments *args,
 IOExternalMethodDispatch *,OSObject *,void *)
{
 IOLockLock(clientLock);IOReturn result=service?service->method(selector,args):kIOReturnNotAttached;IOLockUnlock(clientLock);return result;
}
IOReturn IOHIDEventService::newUserClient(task_t task,void *security,UInt32 type,
 OSDictionary *properties,IOUserClient **handler)
{
 static unsigned logs;if(logs<8U){++logs;IOLog(TOUCH_LOG_PREFIX " client type=%u pid=%d\n",(unsigned)type,proc_selfpid());}
 if(type || !handler)return kIOReturnBadArgument;*handler=0;
 IOS7LeoHIDTouchClient *client=new IOS7LeoHIDTouchClient;if(!client)return kIOReturnNoMemory;
 if(!client->initWithTask(task,security,type,properties)){client->release();return kIOReturnNoMemory;}
 if(!client->attach(this)){client->release();return kIOReturnError;}
 if(!client->start(this)){client->detach(this);client->release();return kIOReturnExclusiveAccess;}
 *handler=client;return kIOReturnSuccess;
}
void ios7leo_register_hid_touch(IOService *platform)
{
 IOHIDEventService *service=new IOHIDEventService;if(!service)return;
 bool attached=service->init() && service->attach(platform);
 if(attached && service->start(platform)){
  service->registerService();service->notePublished();
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
  IOLog("I9100 TOUCH published native HID service\n");
#else
  IOLog(TOUCH_LOG_PREFIX " published actual Type2A service\n");
#endif
 }else if(attached)service->detach(platform);
 service->release();
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
 int homeEnabled=1;
 (void)PE_parse_boot_argn("i9100home",&homeEnabled,sizeof(homeEnabled));
 if(!homeEnabled){IOLog("I9100 HOME disabled by boot argument\n");return;}
 IOHIDEventService *home=new IOHIDEventService;if(!home)return;
 OSDictionary *properties=OSDictionary::withCapacity(1);
 if(!properties){home->release();return;}
 properties->setObject("I9100HomeButton",kOSBooleanTrue);
 bool homeAttached=home->init(properties) && home->attach(platform);properties->release();
 if(homeAttached && home->start(platform)){home->registerService();home->notePublished();}
 else if(homeAttached)home->detach(platform);
 home->release();
#endif
}
#endif
