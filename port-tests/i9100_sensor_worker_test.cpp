/* SPDX-License-Identifier: BSD-2-Clause; actual worker/native/controller/core. */
#include "i9100_native_i2c_model.h"
#include "i9100_sensor_worker.h"
#include "offline-native-sdk/IOKit/i9100_model_events.h"
#include <typeinfo>
static IOWorkLoop *sensor_loop;
static unsigned adds,fail_add,fail_remove,fail_timer;
void *i9100_model_event_allocate(size_t n){return allocate()?calloc(1,n):nullptr;}
void i9100_model_event_free(void *p){assert(p&&active_allocations);active_allocations--;free(p);}
IOWorkLoop *IOWorkLoop::workLoop(){sensor_loop=new IOWorkLoop;return sensor_loop;}
IOReturn IOWorkLoop::addEventSource(IOEventSource *s)
{if(++adds==fail_add)return -99;assert(count<4);s->retain();s->setWorkLoop(this);sources[count++]=s;return 0;}
IOReturn IOWorkLoop::removeEventSource(IOEventSource *s)
{
 if(fail_remove)return -98;unsigned n=0;while(n<count&&sources[n]!=s)n++;assert(n<count);
 s->setWorkLoop(nullptr);s->release();for(unsigned k=n;k+1<count;k++)sources[k]=sources[k+1];sources[--count]=nullptr;return 0;
}
IOReturn IOWorkLoop::runAction(Action action,OSObject *owner,void *a,void *b,void *c,void *d)
{assert(interrupts&&owner&&typeid(*owner)!=typeid(OSObject));bool old=gate;gate=true;int e=action(owner,a,b,c,d);gate=old;return e;}
void IOEventSource::signalWorkAvailable(){assert(workLoop);workLoop->pending=true;}
void IOWorkLoop::pump(){assert(!gate&&interrupts);thread=gate=true;for(unsigned n=0;n<count;n++)sources[n]->checkForWork();thread=gate=false;}
void IOWorkLoop::tick(){assert(!gate&&interrupts);thread=gate=true;for(unsigned n=0;n<count;n++)if(auto *t=dynamic_cast<IOTimerEventSource *>(sources[n]))t->fire();thread=gate=false;}
void IOWorkLoop::free(){assert(!count&&!gate);}
IOReturn IOTimerEventSource::setTimeoutUS(UInt32 n){assert(n>=1000U&&n<=20000U);if(fail_timer)return -97;queued=true;return 0;}
static i9100_sensor_worker_profile profile(unsigned mask)
{
 i9100_sensor_worker_profile p={};p.mask=mask;p.pclk_hz[0]=p.pclk_hz[1]=100000000U;p.poll_us=10000U;p.board_and_clock_verified=1;
 for(unsigned n=0;n<3;n++){p.sensor[n].kind=(i9100_sensor_kind)(n+1);for(unsigned axis=0;axis<3;axis++)p.sensor[n].axis[axis][axis]=1;}
 return p;
}
static i9100_peripheral_native *start_parent()
{sensor_loop=nullptr;adds=fail_add=fail_remove=fail_timer=0;return fresh_i2c_parent();}
static void put_value(uint8_t *d,unsigned reg,int value)
{d[reg]=(uint8_t)value;d[reg+1]=(uint8_t)((unsigned)value>>8);}
static void st_data(unsigned kind,int value,unsigned status=8)
{
 uint8_t *d=i2c_card::hosts[0].data[kind==1?0x19:0x69];d[0x27]=(uint8_t)status;
 put_value(d,0x28,kind==1?value*16:value);put_value(d,0x2a,kind==1?-value*16:-value);put_value(d,0x2c,0);
}
static void mag_data()
{uint8_t *d=i2c_card::hosts[2].data[0x0c];d[2]=1;put_value(d,3,100);put_value(d,5,-200);put_value(d,7,300);d[9]=0;}
static struct i9100_sensor_worker_status status(i9100_sensor_worker *w)
{struct i9100_sensor_worker_status s;assert(!i9100_sensor_worker_status(w,&s));return s;}
static void tick(uint64_t advance=10000000ULL){ns+=advance;sensor_loop->tick();}
static void successful_collection()
{
 i9100_peripheral_native *c=start_parent();i9100_sensor_worker *w=0;auto p=profile(7);
 unsigned before=barriers,live=active_allocations;
 assert(i9100_sensor_worker_create(0,c,&p,&w)==I9100_SENSOR_DISABLED&&!w&&before==barriers&&live==active_allocations);
 assert(!i9100_sensor_worker_create(1,c,&p,&w));auto s=status(w);
 assert(s.ready&&!s.running&&s.bus_owned==3&&s.worker_holds==3&&s.device_holds==7);
 assert(s.id[0]==0x33&&s.id[1]==0xd3&&s.id[2]==0x48&&!c->buses[1].live);
 assert(c->buses[0].borrowers==3&&c->buses[2].borrowers==2);
 assert(i9100_peripheral_native_transport_close(c->buses[0].owner)==I9100_I2C_BUSY);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 assert(!i9100_sensor_worker_start(w));tick();assert(status(w).queued==0&&status(w).converting==4);
 st_data(1,100);st_data(2,100);mag_data();tick();s=status(w);
 assert(s.queued==3&&s.produced==3&&s.converting==4&&!s.fault);
 i9100_sensor_sample samples[4];uint32_t received=99;
 assert(!i9100_sensor_worker_read(w,samples,4,&received)&&received==3);
 assert(samples[0].kind==1&&samples[0].xyz[0]==100000&&samples[0].xyz[1]==-100000);
 assert(samples[1].kind==2&&samples[1].xyz[0]==875000&&samples[1].xyz[1]==-875000);
 assert(samples[2].kind==3&&samples[2].xyz[0]==30000&&samples[2].xyz[1]==-60000&&samples[2].xyz[2]==90000);
 assert(samples[0].timestamp_ns<=samples[1].timestamp_ns&&samples[1].timestamp_ns<=samples[2].timestamp_ns);
 tick();assert(status(w).queued==0&&status(w).produced==3);
 before=barriers;assert(i9100_sensor_worker_read(w,(i9100_sensor_sample *)w,1,&received)==I9100_SENSOR_ARGUMENT&&before==barriers);
 assert(i9100_sensor_worker_status(w,(struct i9100_sensor_worker_status *)w)==I9100_SENSOR_ARGUMENT);
 assert(!i9100_sensor_worker_stop(w));s=status(w);assert(s.closed&&!s.ready&&!s.bus_owned&&!s.device_holds&&!s.worker_holds);
 assert(i9100_sensor_worker_start(w)==I9100_SENSOR_BUSY);
 assert(!i9100_sensor_worker_destroy(&w)&&!w);i2c_restored(c);
}
static void bounded_queue_and_overflow()
{
 i9100_peripheral_native *c=start_parent();i9100_sensor_worker *w=0;auto p=profile(1);
 assert(!i9100_sensor_worker_create(1,c,&p,&w)&&!i9100_sensor_worker_start(w));
 for(unsigned n=1;n<=40;n++){st_data(1,(int)n);tick();}
 auto s=status(w);assert(s.queued==32&&s.produced==40&&s.dropped==8&&!s.fault);
 i9100_sensor_sample samples[32];uint32_t count=0;
 assert(!i9100_sensor_worker_read(w,samples,32,&count)&&count==32);
 for(unsigned n=0;n<32;n++){assert(samples[n].sequence==9U+n&&samples[n].xyz[0]==(int32_t)((9U+n)*1000U));if(n)assert(samples[n].timestamp_ns>samples[n-1].timestamp_ns);}
 st_data(1,100,0x88);tick();s=status(w);assert(s.overflows==1&&!s.queued&&s.produced==40&&!s.fault);
 tick();assert(status(w).produced==40);
 assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
}
static void fault_retention_and_recovery()
{
 i9100_peripheral_native *c=start_parent();i9100_sensor_worker *w=0;auto p=profile(1);
 assert(!i9100_sensor_worker_create(1,c,&p,&w)&&!i9100_sensor_worker_start(w));
 st_data(1,42);tick();i2c_card::host &h=i2c_card::hosts[0];h.stalled=1;tick();h.stalled=0;
 auto s=status(w);assert(s.fault&&!s.running&&s.error==I9100_SENSOR_IO&&s.queued==1&&s.device_holds==1&&s.worker_holds==1);
 assert(i9100_sensor_worker_stop(w)==I9100_SENSOR_IO&&c->buses[0].borrowers==2);
 unsigned writes=h.writes;assert(i9100_sensor_worker_recover_and_stop(w)==I9100_I2C_BUS&&h.writes==writes);
 const i9100_i2c_board_bus &bp=i9100_u1_i2c[0];const i9100_gpio_bank_config &bank=i9100_u1_banks[bp.bank];
 memory[i9100_gpio_physical[bank.region]][(bank.offset+4U)/4]|=(1U<<bp.sda)|(1U<<bp.scl);
 assert(!i9100_sensor_worker_recover_and_stop(w));s=status(w);
 assert(s.closed&&s.fault&&s.error==I9100_SENSOR_IO&&!s.bus_owned&&!s.device_holds&&!s.worker_holds&&!s.running);
 i9100_sensor_sample sample;uint32_t count=0;assert(!i9100_sensor_worker_read(w,&sample,1,&count)&&count==1&&sample.xyz[0]==42000);
 assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
 c=start_parent();w=0;p=profile(1);assert(!i9100_sensor_worker_create(1,c,&p,&w));
 i2c_card::host &sticky=i2c_card::hosts[0];sticky.slave_drop=1;sticky.slave_address=0x19;sticky.slave_register=0x20;sticky.slave_value=0;
 assert(i9100_sensor_worker_stop(w)==I9100_SENSOR_IO&&status(w).device_holds==1&&c->buses[0].live);
 sticky.slave_drop=0;assert(!i9100_sensor_worker_recover_and_stop(w));assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
}
static void magnet_timeout_and_busy_bound()
{
 i9100_peripheral_native *c=start_parent();i9100_sensor_worker *w=0;auto p=profile(4);
 assert(!i9100_sensor_worker_create(1,c,&p,&w)&&!i9100_sensor_worker_start(w));tick();tick(25000001ULL);
 auto s=status(w);assert(s.fault&&s.error==I9100_SENSOR_TIMEOUT&&!s.running&&!s.queued&&s.device_holds==4);
 assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
 c=start_parent();w=0;p=profile(1);assert(!i9100_sensor_worker_create(1,c,&p,&w)&&!i9100_sensor_worker_start(w));
 auto *lease=c->buses[0].owner;assert(!owned_i2c_claim(lease));unsigned writes=i2c_card::hosts[0].writes;
 tick();assert(status(w).busy==1&&!status(w).fault);tick(500000001ULL);
 assert(status(w).fault&&!status(w).running&&status(w).error==I9100_SENSOR_TIMEOUT&&writes==i2c_card::hosts[0].writes);
 owned_i2c_release(lease);assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
}
static void allocation_profile_and_detach_failures()
{
 for(unsigned point=1;point<=3;point++){
  i9100_peripheral_native *c=start_parent();i9100_sensor_worker *w=0;auto p=profile(1);
  unsigned live=active_allocations;fail_alloc=allocations+point;
  assert(i9100_sensor_worker_create(1,c,&p,&w)==I9100_SENSOR_IO&&!w&&active_allocations==live);
  fail_alloc=0;i2c_restored(c);
 }
 i9100_peripheral_native *c=start_parent();i9100_sensor_worker *w=0;auto p=profile(1);fail_add=1;
 assert(i9100_sensor_worker_create(1,c,&p,&w)==-99&&!w&&!c->buses[0].live);i2c_restored(c);
 c=start_parent();p=profile(1);p.sensor[0].axis[0][0]=0;
 assert(i9100_sensor_worker_create(1,c,&p,&w)==I9100_SENSOR_ARGUMENT&&!w&&!c->buses[0].live);i2c_restored(c);
 c=start_parent();p=profile(1);p.board_and_clock_verified=0;unsigned before=barriers;
 assert(i9100_sensor_worker_create(1,c,&p,&w)==I9100_SENSOR_ARGUMENT&&!w&&before==barriers);i2c_restored(c);
 c=start_parent();p=profile(1);assert(!i9100_sensor_worker_create(1,c,&p,&w));fail_timer=1;
 assert(i9100_sensor_worker_start(w)==I9100_SENSOR_IO&&status(w).fault&&status(w).bus_owned==1);fail_timer=0;
 assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
 c=start_parent();p=profile(1);assert(!i9100_sensor_worker_create(1,c,&p,&w));
 auto *timer=dynamic_cast<IOTimerEventSource *>(sensor_loop->sources[0]);timer->retain();fail_remove=1;
 assert(i9100_sensor_worker_destroy(&w)==-98&&w&&status(w).closed&&!status(w).bus_owned);
 assert(!i9100_peripheral_native_destroy(&c)&&!c);i2c_parent=0;
 unsigned live=active_allocations;sensor_loop->tick();assert(active_allocations==live);
 fail_remove=0;assert(!i9100_sensor_worker_destroy(&w)&&!w);timer->fire();timer->release();assert(!active_allocations);
 c=start_parent();p=profile(1);i2c_card::hosts[0].data[0x19][0x0f]=0x34;fail_remove=1;
 assert(i9100_sensor_worker_create(1,c,&p,&w)==I9100_SENSOR_ID&&w&&status(w).fault&&status(w).closed&&!status(w).ready);
 assert(status(w).error==I9100_SENSOR_ID&&status(w).cleanup_error==-98&&!c->buses[0].live);
 fail_remove=0;assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
 c=start_parent();p=profile(1);i2c_card::hosts[0].fail_write=1;
 i2c_card::hosts[0].drop_enabled=1;i2c_card::hosts[0].drop_register=16;i2c_card::hosts[0].drop_value=3;
 assert(i9100_sensor_worker_create(1,c,&p,&w)==I9100_I2C_IO&&w&&status(w).bus_owned==1&&!status(w).ready);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 i2c_card::hosts[0].drop_enabled=0;assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
 c=start_parent();p=profile(1);assert(!i9100_sensor_worker_create(1,c,&p,&w));sensor_loop->thread=true;
 assert(i9100_sensor_worker_destroy(&w)==I9100_SENSOR_BUSY&&w);sensor_loop->thread=false;
 assert(!i9100_sensor_worker_destroy(&w));i2c_restored(c);
}
int main()
{
 successful_collection();bounded_queue_and_overflow();fault_retention_and_recovery();magnet_timeout_and_busy_bound();allocation_profile_and_detach_failures();
 puts("PASS real OSObject/IOWorkLoop + native I2C1/7 + Samsung controller + sensor cores: raw samples/units/timestamps,32FIFO/drop/no duplicate,per-device and worker holds,bounded busy/magnet timeout,sticky stop and explicit host-only recovery,alloc/profile/timer/remove failures,parent lifetime; register/slave/IOKit stand-ins, no hardware/CoreMotion proof");
 return 0;
}
