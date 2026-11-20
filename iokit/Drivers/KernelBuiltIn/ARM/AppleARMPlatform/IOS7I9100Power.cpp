#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <IOKit/pwr_mgt/IOPMPowerSource.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/boot.h>
#include <pexpert/device_tree.h>
/* Private shared header; object-only rebuilds do not export new headers. */
#include "../../../../../pexpert/arm/i9100_usb_stats.h"
#include "i9100_power.h"
#include "i9100_charge_adopt.h"
#include "i9100_charge_epoch.h"
#include "i9100_charge_diag.h"
#include "i9100_cdc_log.h"
#include "i9100_power_budget_diag.h"
#include "IOS7LeoGraphicsState.h"
extern "C" {
#include <mach/mach_time.h>
#include <mach/processor_info.h>
#include <kern/clock.h>
#include <kern/thread_call.h>
#include "../../../../../bsd/sys/reboot.h"
extern int boot(int,int,char *);
volatile uint32_t i9100_lpm_poweroff_ready,i9100_lpm_poweroff_request;
static thread_call_t i9100_lpm_poweroff_call;
static void i9100_lpm_poweroff_worker(thread_call_param_t unused0,thread_call_param_t unused1)
{
 (void)unused0;(void)unused1;
 int result=boot(0,RB_HALT,0);
 i9100_lpm_poweroff_request=result?4:3;
}
void _disable_preemption(void);
void _enable_preemption(void);
/* Read-only panel diagnostics; one power workloop is the sole writer. */
volatile uint32_t i9100_power_valid,i9100_power_percent,i9100_power_mv;
volatile uint32_t i9100_power_connected,i9100_power_charging;
volatile uint32_t i9100_power_stop_uncertain;
volatile uint32_t i9100_power_revision_value=0xffffffffU;
volatile int32_t i9100_power_temp;
volatile int32_t i9100_power_reason=I9100_POWER_IO;
volatile uint32_t i9100_power_policy[10];
/* One Power start owns this publication; no new USB, timer or MMIO reader. */
volatile uint32_t i9100_charge_adopt_diag[16]={2};
volatile uint32_t i9100_charge_epoch_live[I9100_CHARGE_EPOCH_WORDS];
volatile uint32_t i9100_cdc_liveness_diag[I9100_CDC_LOG_DIAG_WORDS]={1,0,I9100_USB_STATS_WORDS};
extern processor_t master_processor;
extern uint32_t processor_avail_count;
extern uint32_t hz_tick_interval;
extern int precise_user_kernel_time;
extern kern_return_t processor_info(processor_t, processor_flavor_t, host_t *,
    processor_info_t, mach_msg_type_number_t *);
volatile uint32_t i9100_cpu_load_diag[I9100_POWER_BUDGET_WORDS] = {
    1, 0, I9100_POWER_BUDGET_STRIDE, I9100_POWER_BUDGET_CAPACITY
};
}

static struct i9100_cdc_log_state i9100_cdc_log_state;
static void i9100_power_cdc_report(void)
{
 unsigned s[I9100_USB_STATS_WORDS];i9100_usbcdc_stats(s);
 uint64_t sampled=mach_absolute_time();
 unsigned reason=i9100_cdc_log_decide(&i9100_cdc_log_state,s,sampled);
 i9100_cdc_log_publish(i9100_cdc_liveness_diag,&i9100_cdc_log_state,s,sampled,reason);
 if(reason)
  IOLog("I9100 CDC m=%u in=%u p=%u st=%x rl=%u rx=%u tx=%u b=%u cf=%u h=%u f=%u ul=%u nc=%u gs=%x da=%x dc=%x rc=%u ls=%u oc=%x os=%x ra=%u us=%u ia=%u ig=%x oa=%u re=%u tb=%u\n",
   s[0],s[1],s[2],s[3],s[4],s[5],s[6],s[7],s[8],s[9],s[10],s[11],s[12],s[13],s[14],s[15],s[16],s[17],s[18],s[19],s[20],s[21],s[22],s[23],s[24],s[25],s[26]);
}

/* Ask the native CPU accounting API, including its current-idle adjustment.
 * Do not read private struct offsets or equate a timer IRQ count with load.
 * This reports the master CPU only, with available count retained explicitly. */
static void i9100_cpu_budget_sample(unsigned polls)
{
 processor_t processor=master_processor;
 processor_cpu_load_info_data_t load={0};
 mach_msg_type_number_t count=PROCESSOR_CPU_LOAD_INFO_COUNT;
 host_t host=0;
 kern_return_t result=KERN_FAILURE;
 uint32_t tick=hz_tick_interval,available=processor_avail_count;
 if(processor && tick && available && available<=32)
  result=processor_info(processor,PROCESSOR_CPU_LOAD_INFO,&host,
   (processor_info_t)&load,&count);
 uint64_t now=mach_absolute_time();
 uint32_t words[I9100_POWER_BUDGET_STRIDE-1U]={0};
 words[0]=(uint32_t)now;words[1]=(uint32_t)(now>>32);words[2]=polls;
 words[3]=result==KERN_SUCCESS && count==PROCESSOR_CPU_LOAD_INFO_COUNT;
 words[4]=(uint32_t)result;words[5]=count;words[6]=available;
 words[7]=load.cpu_ticks[CPU_STATE_USER];words[8]=load.cpu_ticks[CPU_STATE_SYSTEM];
 words[9]=load.cpu_ticks[CPU_STATE_IDLE];words[10]=load.cpu_ticks[CPU_STATE_NICE];
 words[11]=tick;words[12]=precise_user_kernel_time!=0;
 words[13]=(uint32_t)(uintptr_t)processor;
 LeoGraphicsState graphics={0};
 if(ios7leo_graphics_snapshot(&graphics)){
  words[14]=1;words[15]=graphics.last_client;
  words[16]=graphics.send_calls;words[17]=graphics.send_success;
  words[18]=(graphics.requested?1U:0U)|(graphics.enabled?2U:0U)|
   (graphics.closed?4U:0U)|(graphics.port_present?8U:0U)|(graphics.callback_present?16U:0U);
 }
 i9100_power_budget_publish(i9100_cpu_load_diag,words);
}

class IOS7I9100Power : public IOPMPowerSource {
 OSDeclareDefaultStructors(IOS7I9100Power)
 IOMemoryDescriptor *device[4];IOMemoryMap *mapping[4];
 volatile uint8_t *regs[4];
 IOWorkLoop *loop;IOTimerEventSource *timer;
 i9100_power_hw hw;i9100_power_state battery;
 unsigned polls;int lastReason;unsigned lastConnected,lastCharging;
 unsigned chargeProbe,chargeTest,chargeTestDone;
 bool supervisionStopped;
 static void probeSleep(unsigned ms){IOSleep(ms);}
 static uint32_t read32(void *,unsigned,unsigned);
 static void write32(void *,unsigned,unsigned,uint32_t);
 static void delay(void *,unsigned);
 static void enter(void *);static void leave(void *);
 static uint64_t now(void *);
 static void tick(OSObject *,IOTimerEventSource *);
 void sample(void);
 void stopSupervision(const char *,bool);
public:
 virtual bool init(void);
 virtual bool start(IOService *);
 virtual void free(void);
};
OSDefineMetaClassAndStructors(IOS7I9100Power,IOPMPowerSource)
bool IOS7I9100Power::init(void)
{
 if(!IOPMPowerSource::init())return false;
 bzero(device,sizeof(device));bzero(mapping,sizeof(mapping));bzero(regs,sizeof(regs));
 bzero(&battery,sizeof(battery));loop=0;timer=0;polls=0;
 lastReason=999;lastConnected=lastCharging=0xffffffffU;
 chargeProbe=chargeTest=chargeTestDone=0;
 supervisionStopped=false;
 hw.cookie=this;hw.read32=read32;hw.write32=write32;hw.delay_us=delay;
 hw.enter=enter;hw.leave=leave;return true;
}
uint32_t IOS7I9100Power::read32(void *cookie,unsigned region,unsigned off)
{
 IOS7I9100Power *s=(IOS7I9100Power *)cookie;
 if(region>=4 || !s->regs[region] || (off&3U) || off>=(region==2?0x10000U:4096U))return 0xffffffffU;
 __asm__ volatile("dsb sy" ::: "memory");
 uint32_t value=*(volatile uint32_t *)(s->regs[region]+off);
 __asm__ volatile("dsb sy" ::: "memory");return value;
}
void IOS7I9100Power::write32(void *cookie,unsigned region,unsigned off,uint32_t value)
{
 IOS7I9100Power *s=(IOS7I9100Power *)cookie;
 if(region>=4 || !s->regs[region] || (off&3U) || off>=(region==2?0x10000U:4096U))return;
 *(volatile uint32_t *)(s->regs[region]+off)=value;
 __asm__ volatile("dsb sy" ::: "memory");
}
void IOS7I9100Power::delay(void *,unsigned us){IODelay(us);}
void IOS7I9100Power::enter(void *){_disable_preemption();}
void IOS7I9100Power::leave(void *){_enable_preemption();}
uint64_t IOS7I9100Power::now(void *){return mach_absolute_time();}
bool IOS7I9100Power::start(IOService *provider)
{
 if(!IOPMPowerSource::start(provider))return false;
 boot_args *args=(boot_args *)PE_state.bootArgs;
 if(!args || args->machineType!=0xdc4U)return false;
 /* Exynos4210 ADC is 0x13910000, NOT Exynos4412's 0x126c0000. */
 const uint32_t address[4]={0x11400000U,0x11000000U,0x10030000U,0x13910000U};
 for(unsigned n=0;n<4;n++){
  device[n]=IOMemoryDescriptor::withPhysicalAddress(address[n],n==2?0x10000U:4096U,kIODirectionInOut);
  mapping[n]=device[n]?device[n]->map(kIOMapInhibitCache):0;
  if(!mapping[n] || !mapping[n]->getAddress())return false;
  regs[n]=(volatile uint8_t *)mapping[n]->getAddress();
 }
 unsigned revision=0xffffffffU;
 if(!PE_parse_boot_argn("i9100rev",&revision,sizeof(revision)))revision=i9100_power_revision(&hw);
 i9100_power_revision_value=revision;
 PE_parse_boot_argn("i9100chargediag",&chargeProbe,sizeof(chargeProbe));
 PE_parse_boot_argn("i9100chargeprobe",&chargeTest,sizeof(chargeTest));
 i9100_power_io io;
 io.cookie=&hw;io.pmic_read=i9100_power_pmic_read;io.pmic_write=i9100_power_pmic_write;
 io.muic_read=i9100_power_muic_read;io.muic_write=i9100_power_muic_write;
 io.fuel_read=i9100_power_fuel_read;io.adc_read=i9100_power_adc_read;
 io.sub_read=i9100_power_sub_read;io.sub_enable=i9100_power_sub_enable;io.now_ns=now;
 int result=i9100_power_init(&battery,&io,revision);
 i9100_power_reason=result;
 IOLog("I9100 POWER rev=%u controller=%s init=%d (USB rate, thermal ADC%u)\n",
  revision,battery.use_sub?"MAX8922":"MAX8997",result,battery.use_sub?7:6);
 if(result)return false;
 /* Preserve the same insertion's protected elapsed period across a supervised
  * loader boot. The handoff is evidence, never a substitute for sample(). */
 unsigned adopted=I9100_CHARGE_ADOPT_NONE;
 int lpm_handoff=0;
 if(PE_parse_boot_argn("i9100lpmloader",&lpm_handoff,sizeof(lpm_handoff)) && lpm_handoff==1){
  DTEntry chosen=0;void *record=0;unsigned record_bytes=0;
  if(DTLookupEntry(0,"/chosen",&chosen)==kSuccess &&
     DTGetProperty(chosen,I9100_CHARGE_HANDOFF_PROPERTY,&record,&record_bytes)==kSuccess)
   adopted=i9100_charge_adopt(&battery,record,record_bytes,now(0));
 }
 i9100_charge_adopt_diag[1]=adopted;i9100_charge_adopt_diag[2]=revision;
 i9100_charge_adopt_diag[3]=(uint32_t)battery.charge_start;
 i9100_charge_adopt_diag[4]=(uint32_t)(battery.charge_start>>32);
 i9100_charge_adopt_diag[5]=battery.full;i9100_charge_adopt_diag[6]=battery.timeout;
 i9100_charge_adopt_diag[7]=battery.recharging;
 uint64_t adopted_now=now(0);
 i9100_charge_adopt_diag[8]=(uint32_t)adopted_now;
 i9100_charge_adopt_diag[9]=(uint32_t)(adopted_now>>32);
 i9100_charge_adopt_diag[10]=battery.enabled;
 i9100_charge_adopt_diag[11]=(uint32_t)battery.charge_elapsed_base;
 i9100_charge_adopt_diag[12]=(uint32_t)(battery.charge_elapsed_base>>32);
 __sync_synchronize();i9100_charge_adopt_diag[15]=1;
 if(adopted!=I9100_CHARGE_ADOPT_NONE)
  IOLog("I9100 POWER handoff=%u inherited_full=%u inherited_timeout=%u recharge=%u\n",
   adopted,battery.full,battery.timeout,battery.recharging);
 loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,tick);
 if(!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess)return false;
 setName("IOS7I9100Battery");setProperty("Built-In",true);
 setProperty("BatteryHardwareRevision",revision,32);
 setProperty("ChargerController",battery.use_sub?"MAX8922":"MAX8997");
 setProperty("FuelGauge","MAX17042");setProperty("ChargingPolicy","USB rate; 4.2V; thermistor and voltage checks");
 setMaxCapacity(100);setTimeRemaining(-1);
 sample();
 if(timer->setTimeoutMS(1000)!=kIOReturnSuccess){
  stopSupervision("initial timer arm failed",true);return false;
 }
 int lpm_supported=0;
 if(PE_parse_boot_argn("i9100lpmloader",&lpm_supported,4) && lpm_supported==1){
  if(!i9100_lpm_poweroff_call)
   i9100_lpm_poweroff_call=thread_call_allocate(i9100_lpm_poweroff_worker,0);
  __sync_synchronize();i9100_lpm_poweroff_ready=i9100_lpm_poweroff_call!=0;
 }
 return true;
}
void IOS7I9100Power::sample(void)
{
 if(i9100_lpm_poweroff_ready && OSCompareAndSwap(1,2,&i9100_lpm_poweroff_request))
  thread_call_enter(i9100_lpm_poweroff_call);
 (void)i9100_power_poll(&battery);polls++;
 /* Preserve exact policy clocks for a normal halt into the same cable's
  * charger. The final PE hook seals this stable publication without IO. */
 struct I9100ChargeEpoch epoch;
 uint64_t rtc_seconds=0,rtc_began=mach_absolute_time();
 unsigned rtc_valid=i9100_power_rtc_seconds(&hw,&rtc_seconds);
 uint64_t rtc_finished=mach_absolute_time();
 rtc_valid=rtc_valid && rtc_finished>=rtc_began && rtc_finished-rtc_began<=500000000ULL;
 i9100_charge_epoch_encode(&epoch,&battery,rtc_finished,polls,rtc_valid,rtc_seconds);
 i9100_charge_epoch_publish(i9100_charge_epoch_live,&epoch);
 if(chargeProbe==1 && chargeTest==1 && !chargeTestDone && polls>=90){
  chargeTestDone=1;
  i9100_charge_probe(&battery,&hw,polls,probeSleep);
 }
 if(chargeProbe==1 && (polls==1 || polls%30==0)){
  i9100_charge_snapshot(&battery,&hw,polls,mach_absolute_time());
  i9100_cpu_budget_sample(polls);
 }
 i9100_power_valid=battery.valid;i9100_power_percent=battery.percent;
 i9100_power_mv=battery.millivolts;i9100_power_connected=battery.connected;
 i9100_power_charging=battery.charging;i9100_power_temp=battery.temperature_deci_c;
 i9100_power_reason=battery.reason;
 i9100_power_stop_uncertain=battery.stop_uncertain;
 i9100_power_policy[0]=1;i9100_power_policy[1]=battery.enabled;
 i9100_power_policy[2]=battery.full;i9100_power_policy[3]=battery.timeout;
 i9100_power_policy[4]=battery.recharging;i9100_power_policy[5]=battery.recharge_low_count;
 i9100_power_policy[6]=(uint32_t)battery.charge_start;i9100_power_policy[7]=(uint32_t)(battery.charge_start>>32);
 i9100_power_policy[8]=(uint32_t)battery.charge_refresh;i9100_power_policy[9]=(uint32_t)(battery.charge_refresh>>32);
 setExternalChargeCapable(battery.valid && battery.connected && battery.present);
 setIsCharging(battery.valid && battery.charging);
 if(battery.valid){
  setExternalConnected(battery.connected);setBatteryInstalled(battery.present);
  setCurrentCapacity(battery.percent);setVoltage(battery.millivolts);
  setAtWarnLevel(battery.percent<=15);setAtCriticalLevel(battery.percent<=5);
 }
 setProperty("MeasurementsValid",battery.valid!=0);
 setProperty("TemperatureDeciC",(uint32_t)battery.temperature_deci_c,32);
 setProperty("ThermistorADC",battery.adc,32);
 setProperty("ChargingControlEnabled",battery.enabled!=0);
 setProperty("ChargingStopReason",(uint32_t)battery.reason,32);
 updateStatus();
 if(polls==1 || battery.reason!=lastReason || battery.connected!=lastConnected ||
  battery.charging!=lastCharging || polls%60==0){
  IOLog("I9100 POWER valid=%u soc=%u mv=%u temp=%d adc=%u cable=%u ct=%u en=%u charging=%u reason=%d errors=%u\n",
   battery.valid,battery.percent,battery.millivolts,battery.temperature_deci_c,battery.adc,
   battery.connected,battery.cable_type,battery.enabled,battery.charging,battery.reason,battery.errors);
  lastReason=battery.reason;lastConnected=battery.connected;lastCharging=battery.charging;
 }
 /* Keep a full RAM snapshot each sample without logging ordinary traffic
  * progress every second. Connection/fault transitions remain immediate. */
 i9100_power_cdc_report();
}
void IOS7I9100Power::stopSupervision(const char *cause,bool notify)
{
 /* Failed start, timer and departing source revoke the same live evidence.
  * Preserve the first hardware stop result, including uncertain readback,
  * so immediate teardown cannot silently turn it into success. */
 if(supervisionStopped)return;
 supervisionStopped=true;
 (void)i9100_power_stop(&battery);
 battery.valid=0;battery.reason=I9100_POWER_IO;
 i9100_power_valid=0;i9100_power_charging=0;i9100_power_reason=I9100_POWER_IO;
 i9100_power_stop_uncertain=battery.stop_uncertain;i9100_lpm_poweroff_ready=0;
 struct I9100ChargeEpoch epoch;
 i9100_charge_epoch_encode(&epoch,&battery,mach_absolute_time(),polls,0,0);
 i9100_charge_epoch_publish(i9100_charge_epoch_live,&epoch);
 if(notify){setIsCharging(false);setProperty("MeasurementsValid",false);updateStatus();}
 IOLog("I9100 POWER supervision ended (%s); stop_confirmed=%u, charger supervision unavailable\n",
  cause,battery.stop_uncertain?0U:1U);
}
void IOS7I9100Power::tick(OSObject *owner,IOTimerEventSource *timer)
{
 IOS7I9100Power *s=OSDynamicCast(IOS7I9100Power,owner);
 if(!s || s->supervisionStopped)return;
 s->sample();
 if(timer->setTimeoutMS(1000)!=kIOReturnSuccess){
  s->stopSupervision("timer rearm failed",true);
 }
}
void IOS7I9100Power::free(void)
{
 if(timer)timer->cancelTimeout();if(loop && timer)loop->removeEventSource(timer);
 if(battery.ready)stopSupervision("power source removed",false);
 if(timer)timer->release();if(loop)loop->release();
 for(unsigned n=0;n<4;n++){if(mapping[n])mapping[n]->release();if(device[n])device[n]->release();}
 IOPMPowerSource::free();
}
void ios7i9100_register_power(IOService *platform)
{
 int enabled=1;
 (void)PE_parse_boot_argn("i9100power",&enabled,sizeof(enabled));
 if(!enabled){IOLog("I9100 POWER disabled by boot argument; no kernel charger control\n");return;}
 IOS7I9100Power *s=new IOS7I9100Power;if(!s)return;
 bool attached=s->init() && s->attach(platform);
 if(attached && s->start(platform)){s->registerService();IOLog("I9100 POWER published IOPMPowerSource\n");}
 else if(attached){s->detach(platform);IOLog("I9100 POWER unavailable; check revision/bus diagnostics\n");}
 s->release();
}
#endif
