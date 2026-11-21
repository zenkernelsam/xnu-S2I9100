#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "i9100_power.h"
#include "i9100_charge_diag.h"
#include "i9100_charge_adopt.h"
#include "i9100_charge_epoch.h"
#include "i9100_rtc_time.h"

struct BatteryModel {
 uint8_t pmic[256];uint16_t fuel[256];unsigned adc6=937,adc7=989;
 uint8_t muic[256]={};unsigned muicReads=0,muicWrites=0,muicFailRead=0;
 bool muicFailWrite=false,muicDropWrite=false;
 bool plugged=true,sub=false,adcOK=true,disableStuck=false,mainDisableStuck=false;
 unsigned ops=0,failAt=0,subWrites=0,subStops=0,mainWrites=0,mainStops=0;
 uint64_t time=1000000000ULL;
 BatteryModel(){
  memset(pmic,0,sizeof(pmic));memset(fuel,0,sizeof(fuel));
  pmic[0]=0x77;pmic[0x10]=0x1a;pmic[0x50]=0x8f;pmic[0x51]=0x12;
  pmic[0x52]=0xb3;pmic[0x53]=0xa9;pmic[0x54]=0xd2;pmic[0x55]=0x91;
  fuel[0x21]=0x0092;fuel[9]=48000;fuel[0xff]=50*256;
 }
 bool operation(){return ++ops!=failAt;}
 static int read(void *c,unsigned reg,uint8_t *v){auto m=(BatteryModel *)c;if(!m->operation())return 0;*v=m->pmic[reg];return 1;}
 static int write(void *c,unsigned reg,uint8_t v){
  auto m=(BatteryModel *)c;assert((reg>=0x50 && reg<=0x55) || reg==0x5a);
  if(!m->operation())return 0;
  if(reg==0x51){m->mainWrites++;if(!(v&0xc0)&&m->mainDisableStuck)return 1;
   if(!(v&0xc0))m->mainStops++;}
  m->pmic[reg]=v;return 1;
 }
 static int gauge(void *c,unsigned reg,uint16_t *v){auto m=(BatteryModel *)c;if(!m->operation())return 0;*v=m->fuel[reg];return 1;}
 static int adc(void *c,unsigned channel,unsigned *v){
  auto m=(BatteryModel *)c;assert(channel==6 || channel==7);
  if(!m->operation() || !m->adcOK)return 0;*v=channel==6?m->adc6:m->adc7;return 1;
 }
 static int status(void *c,unsigned *connected,unsigned *charging){
  auto m=(BatteryModel *)c;if(!m->operation())return 0;
  *connected=m->plugged;*charging=m->sub && m->plugged;return 1;
 }
 static int enable(void *c,unsigned en){auto m=(BatteryModel *)c;assert(en<2);m->subWrites++;
  if(!en&&m->disableStuck)return 0;
  m->sub=en;if(!en)m->subStops++;return m->operation();}
 static uint64_t now(void *c){return ((BatteryModel *)c)->time;}
 static int muic_read(void *c,unsigned reg,uint8_t *v){
  auto m=(BatteryModel *)c;assert(reg==0x0d || reg==5);
  if(++m->muicReads==m->muicFailRead)return 0;*v=m->muic[reg];return 1;
 }
 static int muic_write(void *c,unsigned reg,uint8_t v){
  auto m=(BatteryModel *)c;assert(reg==0x0d);m->muicWrites++;
  if(m->muicFailWrite)return 0;if(!m->muicDropWrite)m->muic[reg]=v;return 1;
 }
 i9100_power_io io(){i9100_power_io io={};io.cookie=this;io.pmic_read=read;io.pmic_write=write;
  io.fuel_read=gauge;io.adc_read=adc;io.sub_read=status;io.sub_enable=enable;io.now_ns=now;return io;}
 i9100_power_io with_muic(){auto value=io();value.muic_read=muic_read;value.muic_write=muic_write;return value;}
};
static void muic_checks()
{
 for(unsigned rev: {7U,14U})for(unsigned inherited=0;inherited<256;inherited++){
  BatteryModel m;m.muic[0x0d]=(uint8_t)inherited;m.muic[5]=1;
  auto io=m.with_muic();i9100_power_state s;assert(!i9100_power_init(&s,&io,rev));
  assert(m.muic[0x0d]==inherited && !m.muicReads && !m.muicWrites);
  assert(!i9100_power_poll(&s) && s.valid && s.enabled && s.charging);
  unsigned want=(inherited|4U)&~1U;
  assert(m.muic[0x0d]==want && ((m.muic[0x0d]^inherited)&~5U)==0);
  assert(m.muicWrites==(want!=inherited));
  unsigned writes=m.muicWrites;uint64_t deadline=s.charge_start;
  m.time+=1000000000ULL;assert(!i9100_power_poll(&s) && s.charge_start==deadline);
  assert(m.muicWrites==writes);
  m.plugged=false;m.pmic[0x10]=0;
  assert(!i9100_power_poll(&s) && !s.enabled && !s.connected && m.muicWrites==writes);
  m.plugged=true;m.pmic[0x10]=0x1a;m.muic[0x0d]=(uint8_t)((inherited|1U)&~4U);
  assert(!i9100_power_poll(&s) && s.enabled && m.muic[0x0d]==want);
 }
 for(unsigned rev: {7U,14U})for(unsigned fault=0;fault<4;fault++){
  BatteryModel m;m.muic[0x0d]=0xfb;m.muic[5]=1;
  auto io=m.with_muic();i9100_power_state s;assert(!i9100_power_init(&s,&io,rev));
  s.charge_start=1;s.charge_elapsed_base=2000000000ULL;
  if(fault==0)m.muicFailRead=1;if(fault==1)m.muicFailWrite=true;
  if(fault==2)m.muicDropWrite=true;if(fault==3)m.muicFailRead=2;
  assert(i9100_power_poll(&s)==I9100_POWER_IO && !s.valid && !s.enabled && !m.sub);
  assert(!(m.pmic[0x51]&0xc0) && !s.stop_uncertain && s.ready);
  assert(s.charge_start==1 && s.charge_elapsed_base==2000000000ULL);
  m.muicFailRead=0;m.muicFailWrite=m.muicDropWrite=false;
  m.time+=1000000000ULL;assert(!i9100_power_poll(&s) && s.enabled);
  assert(m.muic[0x0d]==0xfe && s.charge_start==1 && s.charge_elapsed_base==2000000000ULL);
 }
 for(unsigned mode=0;mode<5;mode++){
  BatteryModel m;m.muic[0x0d]=0xfb;auto io=m.with_muic();i9100_power_state s;
  assert(!i9100_power_init(&s,&io,14));
  if(mode==0)m.plugged=false;if(mode==1)m.adc7=603;
  if(mode==2)m.fuel[0]=8;if(mode==3)s.timeout=1;
  if(mode==4){m.fuel[9]=53760;m.fuel[0xff]=100*256;}
  assert(!i9100_power_poll(&s) && !s.enabled && !m.muicReads && !m.muicWrites && m.muic[0x0d]==0xfb);
 }
 puts("MUIC: all 256 inherited CONTROL2 bytes on both charger boards preserve ACCDET/other fields; attached LowPwr0/CPEn1 readback, faults stop without losing deadlines, detach and blocked samples leave routing alone");
}
static I9100ChargeHandoff handoff_record(unsigned revision,uint64_t sampled_seconds,
 unsigned elapsed,unsigned recharge=0)
{
 I9100ChargeHandoff r={};
 r.magic=I9100_CHARGE_HANDOFF_MAGIC;r.version=1;r.bytes=sizeof(r);r.flags=7;
 r.revision=revision;r.sub_charger=revision>=8U;r.polls=15;
 r.valid=r.present=r.connected=1;r.percent=50;r.millivolts=3750;r.temperature_deci_c=250;
 r.enabled_before=r.charging_before=1;r.qualified_samples=11;r.recovery_seconds=10;
 r.elapsed_seconds=elapsed;r.recharging=recharge;
 uint64_t ticks=sampled_seconds*24000000ULL;
 r.sampled_low=(uint32_t)ticks;r.sampled_high=(uint32_t)(ticks>>32);
 ticks+=24U;r.stopped_low=(uint32_t)ticks;r.stopped_high=(uint32_t)(ticks>>32);
 ticks+=24U;r.published_low=(uint32_t)ticks;r.published_high=(uint32_t)(ticks>>32);
 r.checksum=i9100_charge_handoff_checksum(&r);return r;
}
static void handoff_checks()
{
 for(unsigned rev: {7U,8U,14U}){
  for(unsigned recharge: {0U,1U}){
   BatteryModel m;i9100_power_state s;auto io=m.io();assert(!i9100_power_init(&s,&io,rev));
   unsigned limit=recharge?5400U:18000U;
   I9100ChargeHandoff r=handoff_record(rev,limit-2U,limit-4U,recharge);
   m.time=(uint64_t)(limit-1U)*1000000000ULL;
   unsigned operations=m.ops;
   assert(i9100_charge_adopt(&s,&r,sizeof(r),m.time)==I9100_CHARGE_ADOPT_APPLIED);
   assert(m.ops==operations && !s.enabled && !s.charging && !s.timeout);
   assert(s.charge_start==1000000000ULL && s.recharging==recharge);
   assert(!i9100_power_poll(&s) && s.enabled && !s.timeout);
   m.time=(uint64_t)(limit+1U)*1000000000ULL;
   assert(!i9100_power_poll(&s) && s.timeout && !s.enabled && !m.sub);
   // A real cable detach remains the only timeout-reset path.
   m.plugged=false;m.pmic[0x10]=0;
   assert(!i9100_power_poll(&s) && !s.timeout && !s.charge_start);
   m.plugged=true;m.pmic[0x10]=0x1a;
   assert(!i9100_power_poll(&s) && s.enabled && s.charge_start==m.time);
  }
  {
   BatteryModel m;i9100_power_state s;auto io=m.io();assert(!i9100_power_init(&s,&io,rev));
   auto r=handoff_record(rev,18000,17999);m.time=18002ULL*1000000000ULL;
   assert(i9100_charge_adopt(&s,&r,sizeof(r),m.time)==I9100_CHARGE_ADOPT_EXPIRED);
   assert(s.timeout && !s.enabled && !i9100_power_poll(&s) && !s.enabled);
  }
  {
   BatteryModel m;i9100_power_state s;auto io=m.io();assert(!i9100_power_init(&s,&io,rev));
   auto r=handoff_record(rev,100,0);r.full=1;r.enabled_before=r.charging_before=0;
   r.reason=(uint32_t)I9100_POWER_FULL;r.checksum=i9100_charge_handoff_checksum(&r);
   m.time=102ULL*1000000000ULL;
   assert(i9100_charge_adopt(&s,&r,sizeof(r),m.time)==I9100_CHARGE_ADOPT_APPLIED);
   assert(!i9100_power_poll(&s) && s.full && !s.enabled && s.recharge_low_count==1);
   assert(!i9100_power_poll(&s) && !s.full && s.recharging && s.enabled);
  }
  {
   BatteryModel m;i9100_power_state s;auto io=m.io();assert(!i9100_power_init(&s,&io,rev));
   auto r=handoff_record(rev,100,0);r.connected=r.enabled_before=r.charging_before=0;
   r.reason=(uint32_t)I9100_POWER_OFFLINE;r.checksum=i9100_charge_handoff_checksum(&r);
   m.time=102ULL*1000000000ULL;
   assert(i9100_charge_adopt(&s,&r,sizeof(r),m.time)==I9100_CHARGE_ADOPT_OFFLINE && !s.charge_start);
   assert(!i9100_power_poll(&s) && s.enabled && s.charge_start==m.time);
  }
 }
 for(unsigned bad=0;bad<15;bad++){
  BatteryModel m;i9100_power_state s;auto io=m.io();assert(!i9100_power_init(&s,&io,14));
  auto r=handoff_record(14,100,90);m.time=102ULL*1000000000ULL;
  unsigned bytes=sizeof(r);
  if(bad==0)r.magic=0;if(bad==1)r.revision=7;if(bad==2)r.sub_charger=0;
  if(bad==3)r.enabled_after=1;if(bad==4)r.qualified_samples=10;
  if(bad==5)r.recovery_seconds=9;if(bad==6)r.timeout=1;if(bad==7)r.valid=0;
  if(bad==8)r.elapsed_seconds=86401;if(bad==9)r.temperature_deci_c=UINT32_MAX;
  if(bad==10)r.published_high=1;if(bad==11)m.time=99ULL*1000000000ULL;
  if(bad==12)bytes=124;if(bad==13)r.fault=1;
  r.checksum=i9100_charge_handoff_checksum(&r);
  if(bad==14)r.checksum^=1;
  assert(i9100_charge_adopt(&s,&r,bytes,m.time)==I9100_CHARGE_ADOPT_REJECTED);
  assert(s.timeout && !s.enabled && !i9100_power_poll(&s) && !s.enabled && !m.sub);
 }
 {
  BatteryModel m;i9100_power_state s;auto io=m.io();assert(!i9100_power_init(&s,&io,14));
  auto r=handoff_record(14,20,19);m.time=22000000000ULL;
  assert(i9100_charge_adopt(&s,&r,sizeof(r),m.time)==I9100_CHARGE_ADOPT_APPLIED);
  assert(s.charge_start==1U); // First partial second must not restart its period.
  assert(s.charge_elapsed_base==1U);
  m.adcOK=false;assert(i9100_power_poll(&s)<0 && !s.enabled);
 }
 {
  BatteryModel m;i9100_power_state s;auto io=m.io();assert(!i9100_power_init(&s,&io,14));
  uint8_t zero[128]={};
  assert(i9100_charge_adopt(&s,0,0,m.time)==I9100_CHARGE_ADOPT_NONE);
  assert(i9100_charge_adopt(&s,zero,sizeof(zero),m.time)==I9100_CHARGE_ADOPT_NONE);
  assert(!s.timeout && !i9100_power_poll(&s) && s.enabled);
 }
 assert(!i9100_charge_ticks_ns(UINT64_MAX));
 puts("Handoff policy: inherited 5h/90min deadlines, full-stop restart, detach reset, expired/malformed stop, fresh ADC refusal and ordinary boots passed");
}
static void policy_checks()
{
 for(unsigned rev=0;rev<16;rev++){
  BatteryModel m;i9100_power_state s;i9100_power_io io=m.io();
  assert(i9100_power_init(&s,&io,rev)==0 && s.use_sub==(rev>=8));
  assert((m.pmic[0x51]&0xc0)==0 && !m.sub);
  if(rev<8){
   assert(m.pmic[0x52]==0xb0 && m.pmic[0x53]==0xa5 && m.pmic[0x54]==0xd5);
   assert(m.pmic[0x50]==0xaf && m.pmic[0x55]==0x91 && (m.pmic[0x5a]&0x40));
  }else assert(m.pmic[0x52]==0xb3 && m.pmic[0x53]==0xa9); // Do not tune unused charger.
  int rc=i9100_power_poll(&s);
  if(rc || !s.valid || !s.present || !s.connected || !s.charging || !s.enabled)
   fprintf(stderr,"revision=%u rc=%d valid=%u present=%u cable=%u charging=%u enable=%u reason=%d temp=%d mv=%u\n",rev,rc,s.valid,s.present,s.connected,s.charging,s.enabled,s.reason,s.temperature_deci_c,s.millivolts);
  assert(rc==0 && s.valid && s.present && s.connected && s.charging && s.enabled);
  assert(s.percent==50 && s.millivolts==3750 && s.temperature_deci_c==250);
  if(rev>=8)assert(m.sub && !(m.pmic[0x51]&0xc0));
  else assert(!m.sub && !m.subWrites && (m.pmic[0x51]&0xc0)==0xc0);
  m.adcOK=false;assert(i9100_power_poll(&s)<0 && !s.valid && !s.enabled && !m.sub && !(m.pmic[0x51]&0xc0));
 }
 for(unsigned rev: {7U,8U}){
  BatteryModel m;i9100_power_state s;i9100_power_io io=m.io();assert(!i9100_power_init(&s,&io,rev));
  assert(!i9100_power_poll(&s));
  (rev<8?m.adc6:m.adc7)=rev<8?544:603;
  assert(!i9100_power_poll(&s) && s.reason==I9100_POWER_TEMPERATURE && !s.enabled);
  (rev<8?m.adc6:m.adc7)=rev<8?570:630; // 43C: hysteresis holds the stop.
  assert(!i9100_power_poll(&s) && !s.enabled);
  (rev<8?m.adc6:m.adc7)=rev<8?937:989;
  assert(!i9100_power_poll(&s) && s.enabled);
  m.fuel[9]=53760;assert(!i9100_power_poll(&s) && s.full && !s.enabled && s.reason==I9100_POWER_FULL);
  m.fuel[9]=52992;m.fuel[0xff]=94*256; // 4.14V is above the conservative recharge threshold.
  assert(!i9100_power_poll(&s) && !s.enabled && !s.recharge_low_count);
  m.fuel[9]=48000;(rev<8?m.adc6:m.adc7)=rev<8?570:630;
  assert(!i9100_power_poll(&s) && !s.enabled && !s.recharge_low_count); // 43C cannot start a new cycle.
  (rev<8?m.adc6:m.adc7)=rev<8?937:989;
  m.fuel[9]=48000;assert(!i9100_power_poll(&s) && !s.enabled && s.recharge_low_count==1);
  (rev<8?m.adc6:m.adc7)=rev<8?544:603;
  assert(!i9100_power_poll(&s) && !s.enabled && !s.recharge_low_count); // Hot sample breaks the consecutive count.
  (rev<8?m.adc6:m.adc7)=rev<8?937:989;
  assert(!i9100_power_poll(&s) && !s.enabled && s.recharge_low_count==1);
  assert(!i9100_power_poll(&s) && s.enabled && !s.full && s.recharging); // Recharge without USB detach.
  uint64_t started=s.charge_start;m.time+=600ULL*1000000000ULL;
  assert(!i9100_power_poll(&s) && s.enabled && s.charge_start==started && s.charge_refresh==m.time);
  m.time=started+5400ULL*1000000000ULL;
  assert(!i9100_power_poll(&s) && s.timeout && !s.enabled);
  assert(!i9100_power_poll(&s) && s.timeout && !s.enabled); // Never auto-clear timeout.
  m.plugged=false;m.pmic[0x10]=0;assert(!i9100_power_poll(&s) && !s.full && !s.connected);
  m.plugged=true;m.pmic[0x10]=0x1a;assert(!i9100_power_poll(&s) && s.enabled);
  m.time+=18000ULL*1000000000ULL;
  assert(!i9100_power_poll(&s) && s.timeout && !s.enabled && s.reason==I9100_POWER_TIMEOUT);
 }
 BatteryModel m;i9100_power_state s;i9100_power_io io=m.io();
 assert(i9100_power_init(&s,&io,0xffffffffU)==I9100_POWER_REVISION && !m.ops && !m.subWrites);
 unsigned faultChecks=0;
 for(unsigned rev: {7U,8U}){
  BatteryModel baseline;i9100_power_state test;auto baseIO=baseline.io();assert(!i9100_power_init(&test,&baseIO,rev));
  baseline.ops=0;assert(!i9100_power_poll(&test));unsigned steps=baseline.ops;
  for(unsigned fail=1;fail<=steps;fail++){
   BatteryModel fault;auto faultIO=fault.io();assert(!i9100_power_init(&test,&faultIO,rev));
   fault.ops=0;fault.failAt=fail;assert(i9100_power_poll(&test)<0);
   assert(!test.valid && !test.enabled && !fault.sub && !(fault.pmic[0x51]&0xc0));faultChecks++;
  }
 }
 for(unsigned invalid: {0U,65535U}){
  BatteryModel fault;auto faultIO=fault.io();assert(!i9100_power_init(&s,&faultIO,8));
  fault.fuel[0x21]=invalid;assert(i9100_power_poll(&s)<0 && !s.enabled);
 }
 for(unsigned which=0;which<6;which++){
  BatteryModel fault;auto faultIO=fault.io();assert(!i9100_power_init(&s,&faultIO,8));
  if(which==0)fault.pmic[0x10]|=4;
  if(which==1)fault.fuel[0]|=8;
  if(which==2)fault.fuel[9]=56000;
  if(which==3)fault.fuel[0xff]=0xffff;
  if(which==4)fault.adc7=0;
  if(which==5)fault.adc7=4095;
  (void)i9100_power_poll(&s);assert(!s.enabled && !s.charging && !fault.sub);
  if(which<2)assert(s.valid && !s.present && s.reason==I9100_POWER_NO_BATTERY);
  else assert(!s.valid && s.reason==I9100_POWER_IO);
 }
 int temp;assert(!i9100_power_temperature(6,0,&temp) && !i9100_power_temperature(7,4095,&temp));
 assert(i9100_power_temperature(6,937,&temp) && temp==250);
 assert(i9100_power_temperature(7,989,&temp) && temp==250);
 printf("Charging policy: 16 revisions, full-stop recharge, 90min maintenance bound, refresh preserving deadline, timeout latch, voltage/thermal guards and %u injected I/O failures passed\n",faultChecks);
}

static void failed_stop_recovery_checks()
{
 BatteryModel m;auto io=m.io();i9100_power_state state;
 assert(!i9100_power_init(&state,&io,14)&&!i9100_power_poll(&state));
 uint64_t start=state.charge_start,refresh=state.charge_refresh;
 m.disableStuck=true;m.adcOK=false;m.time+=1000000000ULL;
 assert(i9100_power_poll(&state)==I9100_POWER_IO && !state.valid && !state.enabled);
 assert(state.stop_uncertain);
 assert(m.sub); // Hardware failed to stop despite the software enabled=0.
 m.adcOK=true;m.time+=1000000000ULL;
 unsigned requested=m.subWrites;
 assert(i9100_power_poll(&state)==I9100_POWER_IO && state.stop_uncertain && !state.enabled);
 assert(m.subWrites==requested+1U&&m.sub&&state.charge_start==start);
 I9100ChargeEpoch epoch;i9100_charge_epoch_encode(&epoch,&state,m.time,1,1,900000000);
 assert(epoch.flags!=3U); // Unknown stop state cannot qualify a new epoch.
 m.adcOK=true;m.disableStuck=false;m.time+=1000000000ULL;
 unsigned stops=m.subStops;
 assert(!i9100_power_poll(&state)&&state.valid&&state.enabled&&state.charging);
 assert(m.subStops>stops); // Recovery must confirm disable BEFORE enabling again.
 assert(!state.stop_uncertain);
 assert(state.charge_start==start&&state.charge_refresh==m.time&&refresh<m.time);
 for(unsigned mode=0;mode<3;mode++){
  BatteryModel chip;auto nio=chip.io();i9100_power_state next;
  assert(!i9100_power_init(&next,&nio,14)&&!i9100_power_poll(&next));
  uint64_t original=next.charge_start;chip.adcOK=false;chip.disableStuck=true;chip.time+=1000000000ULL;
  assert(i9100_power_poll(&next)<0&&next.stop_uncertain);
  chip.adcOK=true;chip.disableStuck=false;
  if(mode==0)chip.time=original+18000000000000ULL;
  if(mode==1)chip.adc7=603;
  if(mode==2){chip.fuel[9]=53760;chip.fuel[0xff]=100*256;}
  unsigned writes=chip.subWrites;
  assert(!i9100_power_poll(&next)&&!next.enabled&&!next.stop_uncertain&&!chip.sub);
  assert(chip.subWrites==writes+1U);
  assert(next.reason==(mode==0?I9100_POWER_TIMEOUT:mode==1?I9100_POWER_TEMPERATURE:I9100_POWER_FULL));
  if(mode!=2)assert(next.charge_start==original);
 }
 {
  BatteryModel chip;auto nio=chip.io();i9100_power_state next;
  assert(!i9100_power_init(&next,&nio,7)&&!i9100_power_poll(&next));
  uint64_t original=next.charge_start;chip.adcOK=false;chip.mainDisableStuck=true;chip.time+=1000000000ULL;
  assert(i9100_power_poll(&next)==I9100_POWER_IO&&next.stop_uncertain&&!next.enabled);
  assert((chip.pmic[0x51]&0xc0)==0xc0); // ACK did not change the actual main charger.
  chip.adcOK=true;chip.time+=1000000000ULL;unsigned writes=chip.mainWrites;
  assert(i9100_power_poll(&next)==I9100_POWER_IO&&next.stop_uncertain);
  assert(chip.mainWrites==writes+1U&&next.charge_start==original);
  chip.mainDisableStuck=false;chip.time+=1000000000ULL;unsigned stopped=chip.mainStops;
  assert(!i9100_power_poll(&next)&&next.enabled&&next.charging&&!next.stop_uncertain);
  assert(chip.mainStops==stopped+1U&&(chip.pmic[0x51]&0xc0)==0xc0&&next.charge_start==original);
 }
 puts("Failed-stop recovery: healthy measurements cannot bypass a checked charger stop or reset the safety period");
}

static void epoch_checks()
{
 for(unsigned rev: {7U,8U,14U}){
  for(unsigned recharge: {0U,1U}){
   BatteryModel source;auto io=source.io();i9100_power_state before;
   assert(!i9100_power_init(&before,&io,rev) && !i9100_power_poll(&before));
   before.recharging=recharge;source.time=300000000000ULL;
   I9100ChargeEpoch r;i9100_charge_epoch_encode(&r,&before,source.time,1,1,900000000);
   volatile uint32_t live[32]={};uint32_t storage[96];memset(storage,0xa5,sizeof(storage));
   i9100_charge_epoch_publish(live,&r);
   unsigned checksum=i9100_charge_epoch_seal(storage+32,live,source.time+1000000000ULL);
   for(unsigned n=0;n<32;n++)assert(storage[n]==0xa5a5a5a5 && storage[n+64]==0xa5a5a5a5);
   I9100ChargeEpoch sealed;assert(i9100_charge_epoch_read(storage+32,&sealed) && sealed.route==1 && sealed.checksum==checksum);
   BatteryModel destination;auto afterIO=destination.io();i9100_power_state after;
   assert(!i9100_power_init(&after,&afterIO,rev));destination.time=8000000000ULL;
   unsigned operations=destination.ops;
   assert(i9100_charge_epoch_restore(&after,&sealed,1,destination.time,1,900000010)==I9E_RESTORED);
   assert(destination.ops==operations && !after.enabled && after.recharging==recharge);
   assert(after.charge_start==destination.time && after.charge_elapsed_base==311000000000ULL);
   assert(!i9100_power_poll(&after) && after.enabled);
   destination.time=after.charge_start+(recharge?5400ULL:18000ULL)*1000000000ULL-after.charge_elapsed_base;
   assert(!i9100_power_poll(&after) && after.timeout && !after.enabled && !destination.sub);
   destination.plugged=false;destination.pmic[0x10]=0;
   assert(!i9100_power_poll(&after) && !after.timeout && !after.charge_elapsed_base && !after.refresh_elapsed_base);
  }
 }
 for(unsigned mode=0;mode<8;mode++){
  BatteryModel m;auto io=m.io();i9100_power_state before;assert(!i9100_power_init(&before,&io,14) && !i9100_power_poll(&before));
  m.time=17999000000000ULL;I9100ChargeEpoch r;
  if(mode==1){before.full=1;before.enabled=before.charging=0;before.charge_start=before.charge_refresh=0;}
  if(mode==2){before.timeout=1;before.enabled=before.charging=0;}
  if(mode==3){before.thermal_block=1;before.enabled=before.charging=0;}
  i9100_charge_epoch_encode(&r,&before,m.time,1,1,900000000);
  volatile uint32_t live[32],slot[32];i9100_charge_epoch_publish(live,&r);
  i9100_charge_epoch_seal(slot,live,m.time);assert(i9100_charge_epoch_read(slot,&r));
  if(mode==4)r.checksum^=1;
  if(mode==5)r.flags=0;
  BatteryModel newer;auto newerIO=newer.io();i9100_power_state after;assert(!i9100_power_init(&after,&newerIO,14));
  uint64_t rtc=mode==6?899999999:900000010;
  unsigned result=i9100_charge_epoch_restore(&after,&r,1,newer.time,mode!=7,rtc);
  if(mode==0 || mode==2)assert(result==I9E_EXPIRED && after.timeout);
  else if(mode==1)assert(result==I9E_RESTORED && after.full && !after.charge_start && !after.charge_elapsed_base);
  else if(mode==3)assert(result==I9E_EXPIRED && after.thermal_block && after.timeout);
  else assert(result==I9E_REJECTED && after.timeout);
  if(result==I9E_EXPIRED || result==I9E_REJECTED)assert(!i9100_power_poll(&after) && !after.enabled);
 }
 {
  BatteryModel m;auto io=m.io();i9100_power_state before;assert(!i9100_power_init(&before,&io,14) && !i9100_power_poll(&before));
  before.charge_elapsed_base=14000000000000ULL;m.time=20000000000ULL;
  I9100ChargeEpoch r;i9100_charge_epoch_encode(&r,&before,m.time,7,1,900000000);
  volatile uint32_t live[32],slot[32];i9100_charge_epoch_publish(live,&r);
  i9100_charge_epoch_seal(slot,live,m.time+601000000000ULL);
  assert(!i9100_charge_epoch_read(slot,&r)); // Stale halt cannot silently become a new period.
  i9100_charge_epoch_encode(&r,&before,m.time,8,0,0);i9100_charge_epoch_publish(live,&r);
  i9100_charge_epoch_seal(slot,live,m.time);assert(!i9100_charge_epoch_read(slot,&r));
  auto handoff=handoff_record(14,20,14019);m.time=22000000000ULL;
  assert(i9100_charge_adopt(&before,&handoff,sizeof(handoff),m.time)==I9100_CHARGE_ADOPT_APPLIED);
  assert(i9100_power_elapsed(before.charge_start,before.charge_elapsed_base,m.time)==14022000000000ULL);
 }
 uint8_t rtc[7]={59,59,23,1,12,26,31};uint64_t a,b;
 assert(i9100_rtc_decode(rtc,2,&a));rtc[0]=0;rtc[1]=0;rtc[2]=0;rtc[4]=1;rtc[5]=27;rtc[6]=1;
 assert(i9100_rtc_decode(rtc,2,&b) && b==a+1);
 rtc[4]=2;rtc[5]=0;rtc[6]=29;assert(i9100_rtc_decode(rtc,2,&b));
 rtc[5]=100;assert(!i9100_rtc_decode(rtc,2,&b));
 rtc[5]=26;rtc[6]=28;assert(i9100_rtc_decode(rtc,2,&b));
 uint8_t bcd[7]={0x59,0x59,0x23,1,0x12,0x26,0x31};
 uint8_t binary[7]={59,59,23,1,12,26,31};
 assert(i9100_rtc_decode(bcd,3,&a) && i9100_rtc_decode(binary,2,&b) && a==b);
 bcd[0]=0;bcd[1]=0;bcd[2]=0;bcd[4]=1;bcd[5]=0x27;bcd[6]=1;
 assert(i9100_rtc_decode(bcd,3,&b) && b==a+1);
 bcd[0]=0x1a;assert(!i9100_rtc_decode(bcd,3,&b));bcd[0]=0;
 assert(!i9100_rtc_decode(bcd,1,&b));bcd[2]=0x24;assert(!i9100_rtc_decode(bcd,3,&b));
 rtc[0]=60;assert(!i9100_rtc_decode(rtc,2,&b));
 puts("Warm-reset charge epoch: exact elapsed plus RTC gap, 5h/90min/full/timeout/thermal inheritance, stale/CRC/RTC refusal and reserved-memory guards passed");
}

/* Byte-level I2C peripheral driven by GPIO edges. Each instance models only
 * one known bus/address; touching another bank fails the test. */
struct PowerGPIO {
 enum Phase {IDLE,RX,SLAVE_ACK,TX,MASTER_ACK};
 uint32_t regs[4][16384];uint8_t memory[256],hapticMemory[256];
 bool withHaptic=false,withRTC=false,dropSlaveWrite=false;uint8_t rtcMemory[256];unsigned currentAddress=0,dropAddress=0,dropRegister=0,dropValue=0;
 unsigned region,base,sda,scl,address;
 Phase phase=IDLE,next=IDLE;bool oldClock=true,oldData=true,slaveLow=false,expectAddress=true;
 bool readMode=false,nack=false,stuckClock=false,stuckData=false,masterACK=false,adcStuck=false;
 bool dropEnablePull=false,dropEnableMux=false,dropEnableData=false;
 unsigned bits=0,ackPhase=0,rxByte=0,registerBytes=0,pointer=0,txByte=0,starts=0,stops=0,waits=0;
 unsigned enters=0,leaves=0,guard=0,adcSamples=0;std::vector<uint8_t> received;
 explicit PowerGPIO(bool fuel,bool haptic=false,bool rtc=false):withHaptic(haptic),withRTC(rtc),region(fuel?1:0),base(fuel?0x1a0:0x40),sda(fuel?0:6),scl(fuel?1:7),address(fuel?0x36:0x66){
  memset(regs,0,sizeof(regs));memset(memory,0,sizeof(memory));memset(hapticMemory,0,sizeof(hapticMemory));memset(rtcMemory,0,sizeof(rtcMemory));currentAddress=address;
  regs[region][base/4]=0;regs[region][(base+4)/4]=0xa5a50000;
  regs[region][(base+8)/4]=0x55660000;
 }
 bool master(unsigned pin)const{
  unsigned function=(regs[region][base/4]>>(pin*4))&15U;
  if(function!=1)return true;
  assert(!(regs[region][(base+4)/4]&(1U<<pin)));return false;
 }
 bool clock()const{return master(scl) && !stuckClock;}
 bool data()const{return master(sda) && !slaveLow && !stuckData;}
 uint8_t *selectedMemory(){return currentAddress==0x06U?rtcMemory:currentAddress==0x48U?hapticMemory:memory;}
 void beginTX(){phase=TX;bits=0;txByte=selectedMemory()[pointer&255];slaveLow=!(txByte&0x80);}
 void acceptedByte(){
  received.push_back((uint8_t)rxByte);next=RX;
  if(expectAddress){expectAddress=false;assert((rxByte>>1)==address||(withHaptic&&(rxByte>>1)==0x48U)||(withRTC&&(rxByte>>1)==0x06U));currentAddress=rxByte>>1;
   readMode=rxByte&1;if(!readMode)registerBytes=0;if(readMode)next=TX;}
  else if(!readMode){if(!registerBytes){pointer=rxByte;registerBytes=1;}else{
   unsigned reg=pointer++&255U;if(!(dropSlaveWrite&&currentAddress==dropAddress&&reg==dropRegister&&rxByte==dropValue))selectedMemory()[reg]=(uint8_t)rxByte;}}
  phase=SLAVE_ACK;ackPhase=0;
 }
 void step(){
  bool clk=clock(),dat=data();
  if(oldClock && clk && oldData!=dat){
   if(!dat){phase=RX;bits=0;rxByte=0;expectAddress=true;slaveLow=false;starts++;}
   else{phase=IDLE;slaveLow=false;stops++;}
  }else if(!oldClock && clk){
   if(phase==RX){rxByte=(rxByte<<1)|dat;if(++bits==8)acceptedByte();}
   else if(phase==TX){if(++bits==8){phase=MASTER_ACK;ackPhase=0;}}
   else if(phase==MASTER_ACK && ackPhase==1)masterACK=!dat;
  }else if(oldClock && !clk){
   if(phase==SLAVE_ACK){
    if(!ackPhase){slaveLow=!nack;ackPhase=1;}
    else{slaveLow=false;if(nack)phase=IDLE;else if(next==TX)beginTX();else{phase=RX;bits=0;rxByte=0;}}
   }else if(phase==TX)slaveLow=!(txByte&(0x80U>>bits));
   else if(phase==MASTER_ACK){if(!ackPhase){slaveLow=false;ackPhase=1;}else if(masterACK){pointer++;beginTX();}else{phase=IDLE;slaveLow=false;}}
  }
  oldClock=clock();oldData=data();
 }
 static uint32_t read(void *cookie,unsigned region,unsigned off){
  auto m=(PowerGPIO *)cookie;assert(region<4 && off<65536 && !(off&3));
  uint32_t value=m->regs[region][off/4];
  if(region==m->region && off==m->base+4){unsigned mask=(1U<<m->sda)|(1U<<m->scl);value=(value&~mask)|(m->clock()?(1U<<m->scl):0)|(m->data()?(1U<<m->sda):0);}
  return value;
 }
 static void write(void *cookie,unsigned region,unsigned off,uint32_t value){
  auto m=(PowerGPIO *)cookie;assert(region<4 && off<65536 && !(off&3));
  assert((region==m->region && (off==m->base || off==m->base+4 || off==m->base+8)) ||
         (region==1 && (off==0x100 || off==0x104 || off==0x108)) ||
         (region==2 && off==0xc950) || (region==3 && (off==0 || off==0x18 || off==0x1c)));
  if(region==1 && off==0x108 && m->dropEnablePull)value=(value&~(3U<<4))|(m->regs[region][off/4]&(3U<<4));
  if(region==1 && off==0x100 && m->dropEnableMux)value=(value&~(15U<<8))|(m->regs[region][off/4]&(15U<<8));
  if(region==1 && off==0x104 && m->dropEnableData)value=(value&~4U)|(m->regs[region][off/4]&4U);
  m->regs[region][off/4]=value;
  if(region==m->region && (off==m->base || off==m->base+4))m->step();
  if(region==3 && off==0 && (value&1U) && !m->adcStuck){
   assert((value&(1U<<16)) && (value&(1U<<14)));m->adcSamples++;
   m->regs[3][0]=(value&~1U)|(1U<<15);m->regs[3][3]=989;
  }
 }
 static void delay(void *c,unsigned){((PowerGPIO *)c)->waits++;}
 static void enter(void *c){auto m=(PowerGPIO *)c;assert(!m->guard);m->guard++;m->enters++;}
 static void leave(void *c){auto m=(PowerGPIO *)c;assert(m->guard==1);m->guard--;m->leaves++;}
 i9100_power_hw io(){i9100_power_hw h={this,read,write,delay,enter,leave};return h;}
};
static void bus_checks()
{
 for(unsigned enable=0;enable<2;enable++)for(unsigned pull=0;pull<4;pull++){
  PowerGPIO g(false);auto hw=g.io();
  const uint32_t con=0xabcde000,dat=0x88,pud=(0x5055U&~(3U<<4))|(pull<<4);
  g.regs[1][0x100/4]=con;g.regs[1][0x104/4]=dat;g.regs[1][0x108/4]=pud;
  assert(i9100_power_sub_enable(&hw,enable));
  assert(g.regs[1][0x100/4]==((con&~(15U<<8))|(1U<<8)));
  assert(g.regs[1][0x104/4]==((dat&~4U)|(enable?4U:0U)));
  assert(g.regs[1][0x108/4]==(pud&~(3U<<4)));
 }
 for(unsigned enable=0;enable<2;enable++)for(unsigned fault=0;fault<3;fault++){
  PowerGPIO g(false);auto hw=g.io();g.regs[1][0x108/4]=0x5055;
  g.regs[1][0x100/4]=0x200;g.regs[1][0x104/4]=enable?0U:4U;
  g.dropEnablePull=fault==0;g.dropEnableMux=fault==1;g.dropEnableData=fault==2;
  assert(!i9100_power_sub_enable(&hw,enable));
 }
 {
  PowerGPIO m(false,false,true);auto hw=m.io();uint64_t seconds=0,expected=0;
  uint8_t values[7]={29,1,13,2,11,26,20};m.rtcMemory[3]=2;
  memcpy(m.rtcMemory+0x10,values,7);uint8_t saved[256];memcpy(saved,m.rtcMemory,256);
  assert(i9100_rtc_decode(values,2,&expected));
  assert(i9100_power_rtc_seconds(&hw,&seconds) && seconds==expected);
  assert(!memcmp(saved,m.rtcMemory,256) && m.enters==m.leaves && !m.guard);
  const std::vector<uint8_t> wire={0x0c,3,0x0d,0x0c,0x10,0x0d,0x0c,0x10,0x0d,0x0c,3,0x0d};
  assert(m.received==wire);
  struct i9100_rtc_read observed;assert(i9100_power_rtc_snapshot(&hw,&seconds,&observed));
  assert(observed.version==1 && !(observed.sequence&1U) && observed.status==1 && observed.read_mask==15);
  assert(observed.controls==0x202 && observed.first_low==0x020d011d && observed.first_high==0x141a0b);
  m.rtcMemory[3]=3;uint8_t bcdTime[7]={0x29,1,0x13,2,0x11,0x26,0x20};
  memcpy(m.rtcMemory+0x10,bcdTime,7);memcpy(saved,m.rtcMemory,256);
  assert(i9100_power_rtc_snapshot(&hw,&seconds,&observed) && seconds==expected);
  assert(observed.status==1 && observed.read_mask==15 && observed.controls==0x303);
  assert(!memcmp(saved,m.rtcMemory,256)); /* Raw observation never changes RTC. */
  m.rtcMemory[0x10]=0x1a;seconds=123;
  assert(!i9100_power_rtc_snapshot(&hw,&seconds,&observed) && seconds==123);
  assert(observed.status==7 && observed.read_mask==15 && observed.first_low==0x0213011a);
  m.rtcMemory[3]=0;assert(!i9100_power_rtc_snapshot(&hw,&seconds,&observed));
  assert(observed.status==7 && observed.controls==0);
  m.rtcMemory[3]=2;m.nack=true;assert(!i9100_power_rtc_seconds(&hw,&seconds));
  assert(i9100_rtc_read_diag[8]==2 && !i9100_rtc_read_diag[2] && !(i9100_rtc_read_diag[1]&1U));
  assert(m.enters==m.leaves && !m.guard);
 }
 for(bool fuel: {false,true}){
  PowerGPIO m(fuel);auto hw=m.io();m.memory[9]=0xa6;m.memory[10]=0x5b;
  uint32_t originalCon=m.regs[m.region][m.base/4],originalPull=m.regs[m.region][(m.base+8)/4];
  uint16_t word=0;uint8_t byte=0;
  if(fuel)assert(i9100_power_fuel_read(&hw,9,&word) && word==0x5ba6);
  else assert(i9100_power_pmic_read(&hw,9,&byte) && byte==0xa6);
  std::vector<uint8_t> expected={(uint8_t)(m.address<<1),9,(uint8_t)((m.address<<1)|1)};
  assert(m.received==expected && m.starts==2 && m.enters==m.leaves && !m.guard);
  assert(m.regs[m.region][m.base/4]==originalCon && m.regs[m.region][(m.base+8)/4]==originalPull);
  if(!fuel){assert(i9100_power_pmic_write(&hw,0x53,5));assert(m.memory[0x53]==5);}
  m.nack=true;word=0xabcd;byte=0xcc;
  if(fuel)assert(!i9100_power_fuel_read(&hw,9,&word) && word==0xabcd);
  else assert(!i9100_power_pmic_read(&hw,9,&byte) && byte==0xcc);
  m.nack=false;m.stuckClock=true;unsigned before=m.waits;
  if(fuel)assert(!i9100_power_fuel_read(&hw,9,&word));else assert(!i9100_power_pmic_read(&hw,9,&byte));
  assert(m.waits-before<1000 && m.enters==m.leaves && !m.guard);
 }
 PowerGPIO m(false);auto hw=m.io();
 m.regs[0][0x104/4]=11;assert(i9100_power_revision(&hw)==11);
 m.regs[0][0x100/4]=1;assert(i9100_power_revision(&hw)==0xffffffffU);
 m.regs[2][0xc950/4]=0xa501; m.regs[3][0]=4;m.regs[3][0x1c/4]=2;
 unsigned adc;assert(i9100_power_adc_read(&hw,7,&adc) && adc==989 && m.adcSamples==5);
 assert(m.regs[2][0xc950/4]==0xa501 && m.regs[3][0]==4 && m.regs[3][0x1c/4]==2);
 assert(!i9100_power_adc_read(&hw,1,&adc));
 assert(i9100_power_charge_adc_read(&hw,&adc));
 assert(m.regs[2][0xc950/4]==0xa501 && m.regs[3][0]==4 && m.regs[3][0x1c/4]==2);
 m.adcStuck=true;assert(!i9100_power_adc_read(&hw,6,&adc) && m.regs[2][0xc950/4]==0xa501 && m.regs[3][0]==4);
 m.regs[1][0x100/4]=0xabcdef00;m.regs[1][0x104/4]=0x88;
 assert(i9100_power_sub_enable(&hw,1) && (m.regs[1][0x104/4]&4));
 assert(i9100_power_sub_enable(&hw,0) && !(m.regs[1][0x104/4]&4));
 assert((m.regs[1][0x100/4]&~(15U<<8))==(0xabcdef00U&~(15U<<8)));
 unsigned connected,charging;assert(i9100_power_sub_read(&hw,&connected,&charging) && connected && charging);
 puts("Power hardware: dedicated addresses, repeated START, ACK/NACK, little-endian fuel words, open-drain/pin restoration, bounded timeout, ADC clock/config restoration, revision and charger GPIO masks passed");
}
static BatteryModel *probeModel;
static unsigned sleepCalls,probeFailure;
static void probe_sleep(unsigned ms)
{
 assert(ms==1000);sleepCalls++;probeModel->time+=1000000000ULL;
 if(probeFailure==1)probeModel->failAt=probeModel->ops+14;
 if(probeFailure==2)probeModel->adc7=600;
}
static void probe_checks()
{
 for(unsigned mode=0;mode<4;mode++){
  BatteryModel m;PowerGPIO gpio(false);auto hw=gpio.io();auto io=m.io();i9100_power_state s;
  assert(!i9100_power_init(&s,&io,14) && !i9100_power_poll(&s));
  if(mode==3)m.time=18000ULL*1000000000ULL;
  unsigned count=i9100_charge_diag[1],writes=m.subWrites;uint64_t start=s.charge_start;
  probeModel=&m;probeFailure=mode;sleepCalls=0;
  i9100_charge_probe(&s,&hw,90,probe_sleep);
  assert(sleepCalls==1 && i9100_charge_diag[1]==count+3);
  auto row=[&](unsigned n){return &i9100_charge_diag[4+((count+n-1)&15)*40];};
  assert(row(1)[37]==1 && row(2)[37]==2 && row(3)[37]==3);
  assert(!row(2)[11] && !row(2)[13]);
  assert(s.charge_start==start);
  if(mode==0)assert(s.enabled && s.charging && m.sub && m.subWrites==writes+2 && !row(3)[38]);
  else assert(!s.enabled && !s.charging && !m.sub && row(3)[38]==3);
 }
 for(unsigned mode=0;mode<9;mode++){
  BatteryModel m;PowerGPIO gpio(false);auto hw=gpio.io();auto io=m.io();i9100_power_state s;
  assert(!i9100_power_init(&s,&io,14) && !i9100_power_poll(&s));
  if(mode==0)s.percent=19;if(mode==1)s.temperature_deci_c=401;
  if(mode==2)s.full=1;if(mode==3)s.timeout=1;if(mode==4)s.connected=0;
  if(mode==5)s.valid=0;if(mode==6)s.revision=13;if(mode==7)s.thermal_block=1;
  if(mode==8)s.millivolts=3699;
  unsigned writes=m.subWrites;probeModel=&m;sleepCalls=0;
  i9100_charge_probe(&s,&hw,90,probe_sleep);
  assert(!sleepCalls && m.subWrites==writes && i9100_charge_probe_result==1);
 }
 {
  BatteryModel m;PowerGPIO gpio(false);auto hw=gpio.io();auto io=m.io();i9100_power_state s;
  assert(!i9100_power_init(&s,&io,14) && !i9100_power_poll(&s));
  m.failAt=m.ops+14;probeModel=&m;sleepCalls=0;
  i9100_charge_probe(&s,&hw,90,probe_sleep);
  assert(!sleepCalls && !m.sub && !s.enabled && i9100_charge_probe_result==2);
 }
 puts("Charge probe: ADC1 restoration, bounded pause, original-policy restore, thermal/timeout/IO refusal and unchanged safety deadline passed");
}
int main(){muic_checks();policy_checks();handoff_checks();epoch_checks();failed_stop_recovery_checks();bus_checks();probe_checks();return 0;}
