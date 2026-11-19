/* SPDX-License-Identifier: BSD-2-Clause; real I2C/sensor/worker/HID pipeline. */
#define main sensor_worker_reference_main
#include "i9100_sensor_worker_test.cpp"
#undef main
#include "i9100_sensor_hid.h"
extern "C" void nanoseconds_to_absolutetime(uint64_t value,uint64_t *out){*out=value/3U;}
static unsigned clock_fail,clock_stale;
static int clock_ticks(void *,uint64_t value,uint64_t *out)
{if(clock_fail)return -1;*out=clock_stale?1U:value/3U;return 0;}
static i9100_sensor_hid_profile hid_profile()
{i9100_sensor_hid_profile p={};p.poll_us=10000;p.sender[0]=71;p.sender[1]=72;p.sender[2]=73;p.clock={nullptr,clock_ticks};return p;}
static struct i9100_sensor_hid_status hid_status(i9100_sensor_hid *p)
{struct i9100_sensor_hid_status s={};assert(!i9100_sensor_hid_status(p,&s));return s;}
static uint32_t hid_word(const i9100_sensor_hid_packet &p,unsigned off)
{return p.bytes[off]|((uint32_t)p.bytes[off+1]<<8)|((uint32_t)p.bytes[off+2]<<16)|((uint32_t)p.bytes[off+3]<<24);}
static void emit_record(const i9100_sensor_hid_record &r)
{
 const char *directory=getenv("I9100_SENSOR_HID_MODEL_OUTPUT");if(!directory)return;
 char path[1024];int n=snprintf(path,sizeof(path),"%s/sensor-%u.bin",directory,r.kind);assert(n>0&&(unsigned)n<sizeof(path));
 FILE *f=fopen(path,"wb");assert(f&&fwrite(r.packet.bytes,1,60,f)==60&&!fclose(f));
 n=snprintf(path,sizeof(path),"%s/sensor-%u.json",directory,r.kind);assert(n>0&&(unsigned)n<sizeof(path));
 f=fopen(path,"w");assert(f);fprintf(f,"{\"kind\":%u,\"sequence\":%u,\"sample_ns\":%llu,\"ticks\":%llu}\n",
  r.kind,r.sequence,(unsigned long long)r.sample_ns,(unsigned long long)r.absolute_ticks);assert(!fclose(f));
}
static void consumer_ownership()
{
 auto *parent=start_parent();i9100_sensor_worker *w=nullptr;auto p=profile(1);
 assert(!i9100_sensor_worker_create(1,parent,&p,&w));i9100_sensor_consumer_lease l={},other={};
 assert(!i9100_sensor_worker_consumer_get(w,&l)&&status(w).consumer_owned);
 assert(i9100_sensor_worker_consumer_get(w,&other)==I9100_SENSOR_BUSY);
 assert(i9100_sensor_worker_start(w)==I9100_SENSOR_BUSY&&i9100_sensor_worker_stop(w)==I9100_SENSOR_BUSY);
 assert(i9100_sensor_worker_destroy(&w)==I9100_SENSOR_BUSY&&w);
 i9100_sensor_sample sample;uint32_t count=99;
 assert(i9100_sensor_worker_read(w,&sample,1,&count)==I9100_SENSOR_BUSY&&count==99);
 auto copied=l;assert(i9100_sensor_worker_consumer_start(&copied)==I9100_SENSOR_ARGUMENT);
 assert(i9100_sensor_worker_consumer_put(&l)==I9100_SENSOR_BUSY);
 assert(!i9100_sensor_worker_consumer_start(&l));st_data(1,10);tick();
 assert(i9100_sensor_worker_consumer_read(&l,(i9100_sensor_sample *)&l,1,&count)==I9100_SENSOR_ARGUMENT);
 assert(i9100_sensor_worker_status(w,(struct i9100_sensor_worker_status *)&l)==I9100_SENSOR_ARGUMENT&&l.owned);
 assert(!i9100_sensor_worker_consumer_read(&l,&sample,1,&count)&&count==1&&sample.xyz[0]==10000);
 assert(!i9100_sensor_worker_consumer_stop(&l,0)&&!i9100_sensor_worker_consumer_put(&l));
 assert(!i9100_sensor_worker_destroy(&w));
 assert(i9100_sensor_worker_consumer_start(&l)==I9100_SENSOR_ARGUMENT);
 assert(i9100_sensor_worker_consumer_read(&l,&sample,1,&count)==I9100_SENSOR_ARGUMENT);
 i2c_restored(parent);
}
static void finish_hid(i9100_sensor_hid **,i9100_sensor_worker **,i9100_peripheral_native *);
static void collector_ownership()
{
 auto *parent=start_parent();auto wp=profile(1);auto hp=hid_profile();i9100_sensor_worker *w=nullptr;i9100_sensor_hid *p=nullptr;
 assert(!i9100_sensor_worker_create(1,parent,&wp,&w));assert(!i9100_sensor_hid_create(1,w,&hp,&p));
 i9100_sensor_hid_hold hold={},other_hold={};i9100_sensor_hid_consumer client={},other_client={};
 assert(!i9100_sensor_hid_hold_get(p,&hold)&&hid_status(p).service_owned);
 assert(i9100_sensor_hid_status(p,(struct i9100_sensor_hid_status *)&hold)==I9100_SH_ARGUMENT&&hold.owned);
 auto copied_hold=hold;assert(i9100_sensor_hid_hold_put(&copied_hold)==I9100_SH_OWNER);
 assert(i9100_sensor_hid_hold_get(p,&other_hold)==I9100_SH_BUSY);
 assert(!i9100_sensor_hid_consumer_get(p,1,&client));auto copied_client=client;
 i9100_sensor_hid_record record;
 assert(i9100_sensor_hid_consumer_read(&client,&record,1,(uint32_t *)&hold)==I9100_SH_ARGUMENT&&hold.owned);
 assert(i9100_sensor_hid_consumer_get(p,1,&other_client)==I9100_SH_BUSY);
 assert(i9100_sensor_hid_consumer_put(&copied_client)==I9100_SH_OWNER);
 assert(i9100_sensor_hid_destroy(&p)==I9100_SH_BUSY&&p);
 assert(!i9100_sensor_hid_consumer_bind_sender(&client,777)&&!i9100_sensor_hid_start(p));
 assert(i9100_sensor_hid_consumer_put(&client)==I9100_SH_BUSY&&i9100_sensor_hid_hold_put(&hold)==I9100_SH_BUSY);
 assert(!i9100_sensor_hid_stop(p));assert(i9100_sensor_hid_hold_put(&hold)==I9100_SH_BUSY);
 assert(!i9100_sensor_hid_consumer_put(&client)&&!i9100_sensor_hid_hold_put(&hold));finish_hid(&p,&w,parent);
 assert(i9100_sensor_hid_hold_put(&hold)==I9100_SH_OWNER);
}
static void finish_hid(i9100_sensor_hid **p,i9100_sensor_worker **w,i9100_peripheral_native *parent)
{assert(!i9100_sensor_hid_destroy(p)&&!*p);assert(!i9100_sensor_worker_destroy(w)&&!*w);i2c_restored(parent);}
static void pipeline_and_queue()
{
 auto *parent=start_parent();i9100_sensor_worker *w=nullptr;i9100_sensor_hid *p=nullptr;auto wp=profile(7);auto hp=hid_profile();
 assert(!i9100_sensor_worker_create(1,parent,&wp,&w));IOWorkLoop *worker_loop=sensor_loop;
 unsigned before=barriers,live=active_allocations;
 assert(i9100_sensor_hid_create(0,w,&hp,&p)==I9100_SH_DISABLED&&!p&&before==barriers&&live==active_allocations);
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));IOWorkLoop *hid_loop=sensor_loop;
 assert(status(w).consumer_owned&&hid_status(p).ready&&!hid_status(p).running);
 assert(!i9100_sensor_hid_start(p));ns+=10000000ULL;worker_loop->tick();hid_loop->tick();
 st_data(1,100);st_data(2,100);mag_data();ns+=10000000ULL;worker_loop->tick();hid_loop->tick();
 auto state=hid_status(p);assert(state.produced==3&&state.queued[0]==1&&state.queued[1]==1&&state.queued[2]==1);
 for(unsigned kind=1;kind<=3;kind++){
  i9100_sensor_hid_record r;uint32_t count=0;assert(!i9100_sensor_hid_read(p,kind,&r,1,&count)&&count==1);
  assert(r.kind==kind&&r.sequence==1&&r.absolute_ticks==r.sample_ns/3U);
  assert(hid_word(r.packet,16)==44&&hid_word(r.packet,20)==(kind==1?13U:kind==2?20U:21U));
  assert(hid_word(r.packet,40)==hp.sender[kind-1]);
  assert(hid_word(r.packet,48)==(kind==1?6553U:kind==2?1000U:1966080U));
  emit_record(r);
 }
 ns+=10000000ULL;worker_loop->tick();hid_loop->tick();assert(hid_status(p).produced==3);
 assert(i9100_sensor_hid_read(p,1,(i9100_sensor_hid_record *)p,1,&before)==I9100_SH_ARGUMENT);
 assert(!i9100_sensor_hid_stop(p)&&hid_status(p).closed&&!status(w).consumer_owned);
 assert(i9100_sensor_hid_start(p)==I9100_SH_BUSY);finish_hid(&p,&w,parent);
 parent=start_parent();wp=profile(1);assert(!i9100_sensor_worker_create(1,parent,&wp,&w));worker_loop=sensor_loop;
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));hid_loop=sensor_loop;assert(!i9100_sensor_hid_start(p));
 for(unsigned turn=1;turn<=40;turn++){st_data(1,(int)turn);ns+=10000000ULL;worker_loop->tick();hid_loop->tick();}
 state=hid_status(p);assert(state.produced==40&&state.queued[0]==16&&state.dropped==24);
 i9100_sensor_hid_record records[16];uint32_t count=0;assert(!i9100_sensor_hid_read(p,1,records,16,&count)&&count==16);
 for(unsigned n=0;n<16;n++)assert(records[n].sequence==25U+n);
 finish_hid(&p,&w,parent);
}
static void clock_and_cleanup_failures()
{
 auto hp=hid_profile();auto wp=profile(1);i9100_sensor_worker *w=nullptr;i9100_sensor_hid *p=nullptr;
 auto *parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));IOWorkLoop *worker_loop=sensor_loop;
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));IOWorkLoop *hid_loop=sensor_loop;assert(!i9100_sensor_hid_start(p));
 st_data(1,5);ns+=10000000ULL;worker_loop->tick();clock_fail=1;hid_loop->tick();clock_fail=0;
 assert(hid_status(p).fault&&hid_status(p).error==I9100_SH_CLOCK&&hid_status(p).closed&&!status(w).bus_owned);
 finish_hid(&p,&w,parent);
 parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));worker_loop=sensor_loop;
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));hid_loop=sensor_loop;assert(!i9100_sensor_hid_start(p));clock_stale=1;
 st_data(1,5);ns+=10000000ULL;worker_loop->tick();hid_loop->tick();
 st_data(1,6);ns+=10000000ULL;worker_loop->tick();hid_loop->tick();clock_stale=0;
 assert(hid_status(p).fault&&hid_status(p).error==I9100_SH_CLOCK&&hid_status(p).produced==1);
 finish_hid(&p,&w,parent);
 parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));assert(!i9100_sensor_hid_start(p));
 auto &host=i2c_card::hosts[0];host.slave_drop=1;host.slave_address=0x19;host.slave_register=0x20;host.slave_value=0;
 assert(i9100_sensor_hid_stop(p)!=0&&hid_status(p).worker_owned&&status(w).consumer_owned&&parent->buses[0].live);
 assert(i9100_sensor_worker_destroy(&w)==I9100_SENSOR_BUSY&&w);
 host.slave_drop=0;assert(!i9100_sensor_hid_recover_and_stop(p));finish_hid(&p,&w,parent);
 for(unsigned point=1;point<=3;point++){
  parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));unsigned live=active_allocations;
  fail_alloc=allocations+point;assert(i9100_sensor_hid_create(1,w,&hp,&p)!=0&&!p&&active_allocations==live);
  fail_alloc=0;assert(!i9100_sensor_worker_destroy(&w));i2c_restored(parent);
 }
 parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));hid_loop=sensor_loop;fail_timer=1;
 assert(i9100_sensor_hid_start(p)==I9100_SENSOR_IO&&hid_status(p).fault&&hid_status(p).error==I9100_SENSOR_IO&&!status(w).consumer_owned);fail_timer=0;
 finish_hid(&p,&w,parent);
 parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));hid_loop=sensor_loop;assert(!i9100_sensor_hid_start(p));
 fail_timer=1;hid_loop->tick();fail_timer=0;
 assert(hid_status(p).fault&&hid_status(p).error==I9100_SH_FAULT&&hid_status(p).closed&&!status(w).consumer_owned);
 finish_hid(&p,&w,parent);
 parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));hid_loop=sensor_loop;
 hid_loop->thread=true;assert(i9100_sensor_hid_destroy(&p)==I9100_SH_BUSY&&p);hid_loop->thread=false;
 finish_hid(&p,&w,parent);
 parent=start_parent();assert(!i9100_sensor_worker_create(1,parent,&wp,&w));assert(!i9100_sensor_hid_create(1,w,&hp,&p));
 fail_remove=1;assert(i9100_sensor_hid_destroy(&p)==-98&&p&&hid_status(p).closed&&!status(w).consumer_owned);
 fail_remove=0;finish_hid(&p,&w,parent);
}
#ifndef I9100_SENSOR_HID_TEST_ENTRY
#define I9100_SENSOR_HID_TEST_ENTRY main
#endif
int I9100_SENSOR_HID_TEST_ENTRY()
{
 i9100_sensor_hid_clock real_clock={};i9100_sensor_hid_native_clock(&real_clock);uint64_t real_ticks=0;
 assert(real_clock.absolute_ticks&&!real_clock.absolute_ticks(real_clock.cookie,123456789,&real_ticks)&&real_ticks==41152263);
 assert(!sensor_worker_reference_main());consumer_ownership();collector_ownership();pipeline_and_queue();clock_and_cleanup_failures();
 puts("PASS actual I2C/sensor/worker -> exclusive HID collector: 60-byte vectors, absolute clock conversion, per-kind16 FIFO/drop/no replay, legacy/copied/stale exclusion, actual stop/clock/timer/cleanup faults, default OFF; hardware/IOKit are facades, no registered service");return 0;
}
