/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_STORAGE_MODEL 1
#define I9100_EMMC_REAL_PARENT_MODEL 1
#define I9100_EMMC_CLOCK_OBSERVER_MODEL 1
#include "i9100_emmc_native_test.cpp"
#include "i9100_emmc_clock_native.h"
#include <pexpert/pexpert.h>
static unsigned observer_enabled,root_enabled,ro_enabled,reference_hz=24000000;
static const char *root_arg,*rootdev_arg;
static const char *clock_source;
extern "C" int PE_parse_boot_argn(const char *key,void *value,int bytes)
{
 if(!strcmp(key,"rd")||!strcmp(key,"rootdev")){
  const char *arg=!strcmp(key,"rd")?root_arg:rootdev_arg;
  if(!arg)return 0;assert(bytes>(int)strlen(arg));memcpy(value,arg,strlen(arg)+1);return 1;
 }
 assert(bytes==4);unsigned v=!strcmp(key,"i9100emmcobserve")?observer_enabled:
  !strcmp(key,"i9100emmcdata")?root_enabled:!strcmp(key,"i9100emmcro")?ro_enabled:
  !strcmp(key,"i9100xxtihz")||!strcmp(key,"i9100xusbxtihz")?reference_hz:0;
 memcpy(value,&v,4);return v!=0;
}
void IOLog(const char *,...){}
void IOLockLock(IOLock *l){host_locks.at(l)->lock();assert(!l->held);l->held=true;}
void IOMemoryDescriptor::release(){assert(refs);if(!--refs){assert(m.descriptors);m.descriptors--;delete this;}}
static void quiet()
{assert(!m.contexts&&!m.locks&&!m.maps&&!m.descriptors);fixture_finish();}
static void observer_setup()
{setup(clock_source); // Native/card model creates a quiet Samsung host, no command writes until probe.
 regs[HOST][EM_PWREN/4]=1;regs[HOST][EM_VERID/4]=0x5342220a;regs[HOST][EM_HCON/4]=1U<<7;
}
int main(int argc,char **argv)
{
 clock_source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(clock_source);observer_setup();
 if(argc==2){
  observer_enabled=1;
  if(!strcmp(argv[1],"conflict-data"))root_enabled=1;
  else if(!strcmp(argv[1],"conflict-legacy"))ro_enabled=1;
  else if(!strcmp(argv[1],"conflict-root"))root_arg="*i9100-system";
  else if(!strcmp(argv[1],"conflict-rootdev"))rootdev_arg="i9100-system";
  else if(!strcmp(argv[1],"bad-root-clock"))reference_hz=26000000;
  else if(!strcmp(argv[1],"busy-hook"))regs[HOST][EM_STATUS/4]=1U<<9;
  else assert(false);
  ios6i9100_register_emmc_clock_observation();assert(!fixture_writes());
  if(!strcmp(argv[1],"busy-hook")){
   assert(i9100_emmc_clock_observation_diag[3]==1&&i9100_emmc_clock_observation_diag[5]==1&&m.maps==5);
   assert((int)i9100_emmc_clock_observation_diag[2]==I9100_EMMC_BUSY);
   assert((int)i9100_emmc_clock_observation_diag[4]==I9100_EMMC_BUSY);
   i9100_emmc_host_lease other={};assert(i9100_emmc_host_acquire(&other)==I9100_EMMC_BUSY);
   /* std::map tears down its model-only pointer container at process exit;
    * keep the mutex reachable just as the real retained observer's lock
    * remains reachable. Do not disable LeakSanitizer or free a live lock. */
   static std::mutex * volatile retained_model_mutex;
   assert(host_locks.size()==1);retained_model_mutex=host_locks.begin()->second;
   assert(retained_model_mutex);
   fixture_finish(); /* Deliberately retained owner is rooted by the actual PE hook. */
  }else{
   assert(!m.count&&!m.maps&&!i9100_emmc_clock_observation_diag[3]);
   int expected=reference_hz==26000000?I9100_EMMC_ARGUMENT:I9100_EMMC_BUSY;
   assert((int)i9100_emmc_clock_observation_diag[2]==expected);quiet();
  }
  unsigned allocations=m.count;ios6i9100_register_emmc_clock_observation();assert(m.count==allocations&&!fixture_writes());
  puts("PASS actual one-shot PE observer rejects conflicting roots or retains the unmodified busy host");return 0;
 }
 assert(argc==1);
 ios6i9100_register_emmc_clock_observation();assert(!m.count&&!m.maps&&!fixture_writes());quiet();
 i9100_emmc_clock_observer *o=nullptr;uint8_t raw[512];struct i9100_emmc_clock_result decoded;
 observer_setup();assert(i9100_emmc_clock_observe_open(0,24000000,24000000,&o)==I9100_EMMC_UNSUPPORTED&&!m.count);quiet();
 observer_setup();assert(!i9100_emmc_clock_observe_open(1,24000000,24000000,&o)&&o&&m.maps==5&&!fixture_writes());
 assert(!i9100_emmc_clock_observe_copy(o,raw)&&!i9100_emmc_clock_decode(raw,512,&decoded)&&decoded.ciu_hz==100000000);
 i9100_emmc_host_lease invalid={7U,0U};assert(i9100_emmc_host_acquire(&invalid)==I9100_EMMC_ARGUMENT);
 i9100_emmc_host_lease other={};assert(i9100_emmc_host_acquire(&other)==I9100_EMMC_BUSY);
 assert(!i9100_emmc_clock_observe_close(&o)&&!o);quiet();
 for(unsigned fault=1;fault<=12;fault++){
  observer_setup();m.fail_at=fault;int e=i9100_emmc_clock_observe_open(1,24000000,24000000,&o);
  assert(e==I9100_EMMC_IO&&!o&&!fixture_writes());quiet();
 }
 observer_setup();regs[CMU][0xc940/4]=0;
 assert(i9100_emmc_clock_observe_open(1,24000000,24000000,&o)==I9100_EMMC_CLOCK&&o&&m.maps==4&&!fixture_writes());
 assert(!i9100_emmc_clock_observe_copy(o,raw)&&i9100_emmc_clock_decode(raw,512,&decoded)==I9100_EMMC_CLOCK);
 assert(!i9100_emmc_clock_observe_close(&o));quiet();
 observer_setup();regs[HOST][EM_STATUS/4]=1U<<9;
 assert(i9100_emmc_clock_observe_open(1,24000000,24000000,&o)==I9100_EMMC_BUSY&&o&&!fixture_writes());
 assert(i9100_emmc_clock_observe_close(&o)==I9100_EMMC_BUSY&&o&&m.maps==5);
 assert(!i9100_emmc_clock_observe_copy(o,raw));
 // Test boundary returns the independently owned host idle; production
 // makes no reset/flush/IRQ writes to achieve this transition.
 regs[HOST][EM_STATUS/4]=0;assert(!i9100_emmc_clock_observe_close(&o));quiet();
 observer_setup();observer_enabled=1;ios6i9100_register_emmc_clock_observation();
 assert(i9100_emmc_clock_observation_diag[3]==1&&!i9100_emmc_clock_observation_diag[4]&&!m.maps&&!fixture_writes());
 assert(!i9100_emmc_clock_decode(i9100_emmc_clock_observation,512,&decoded));quiet();
 puts("PASS actual observer5maps/CMU128KiB+hostlease/noMMCwrites/defaultOFF/OOM/gatedhost/rawcapture/busyretention/optionalPEtrace; IOKit/MMIO/time facade only");return 0;
}
