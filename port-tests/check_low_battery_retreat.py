#!/usr/bin/env python3
"""Exercise production low-cell qualification and its normal-halt dispatch offline."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import hashlib,importlib.util,json,subprocess
repo=Path(__file__).resolve().parents[1]
root=repo.parent;platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
source=platform/'IOS7I9100Power.cpp';text=source.read_text(encoding='utf8')
def function(prefix):
    begin=text.index(prefix);brace=text.index('{',begin);at=brace+1;depth=1
    while depth:depth+=(text[at]=='{')-(text[at]=='}');at+=1
    return text[begin:at]
observe=function('void IOS7I9100Power::observeLowBattery(')
tick=function('void IOS7I9100Power::tick(')
worker=function('static void i9100_lpm_poweroff_worker(')
assert 'observeLowBattery();' in function('bool IOS7I9100Power::start(')
assert 'lastPowerSample=mach_absolute_time();' in function('void IOS7I9100Power::sample(')
out=repo/'out'/('low-battery-retreat-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include "i9100_low_battery.h"
using OSObject=void;
using thread_call_param_t=void*;
static uint32_t i9100_lpm_poweroff_ready,i9100_lpm_poweroff_request;
static int i9100_lpm_poweroff_call=23;
static volatile uint32_t i9100_low_battery_retreat_diag[18]={1};
static uint64_t current_ns;
static unsigned submissions,logs,boots;
static int boot_result;
static uint64_t mach_absolute_time(){return current_ns;}
static bool OSCompareAndSwap(unsigned old,unsigned value,uint32_t *where){
 return __sync_bool_compare_and_swap(where,old,value);
}
static void IOLog(const char *format,...){
 char buffer[256];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
 assert(strstr(buffer,"route=NORMAL_HALT"));logs++;
}
static void thread_call_enter(int call){assert(call==23);submissions++;}
enum {kIOReturnSuccess=0,RB_HALT=8};
static int boot(int arg0,int flags,char *arg2){assert(arg0==0 && flags==RB_HALT && !arg2);boots++;return boot_result;}
struct IOTimerEventSource {bool fails=false;unsigned arms=0;int setTimeoutMS(unsigned n){assert(n==1000);arms++;return fails?1:0;}};
#define OSDynamicCast(type,owner) static_cast<type*>(owner)
struct IOS7I9100Power {
 i9100_power_state battery={};i9100_low_battery_state lowBattery={};
 uint64_t lastPowerSample=0;bool supervisionStopped=false;unsigned samples=0,stops=0;
 void observeLowBattery();
 void sample(){samples++;lastPowerSample=current_ns;}
 void stopSupervision(const char *cause,bool notify){assert(strstr(cause,"timer") && notify);stops++;supervisionStopped=true;}
 static void tick(OSObject*,IOTimerEventSource*);
};
'''+observe+'\n'+tick+'\n'+worker+r'''
static IOS7I9100Power seed(){
 IOS7I9100Power s;s.battery.ready=s.battery.valid=s.battery.present=1;
 s.battery.connected=s.battery.charging=s.battery.enabled=1;
 s.battery.percent=3;s.battery.millivolts=3400;s.lowBattery.enabled=1;
 s.battery.charge_start=12345;s.battery.charge_elapsed_base=400000000000ULL;
 current_ns=1000000000ULL;i9100_lpm_poweroff_ready=1;i9100_lpm_poweroff_request=0;
 memset((void*)i9100_low_battery_retreat_diag,0,sizeof(i9100_low_battery_retreat_diag));
 i9100_low_battery_retreat_diag[0]=1;submissions=logs=boots=0;boot_result=0;return s;
}
static void at(IOS7I9100Power&s,uint64_t now){current_ns=now;s.lastPowerSample=now;s.observeLowBattery();}
int main(int argc,char **argv){
 assert(argc==2);alarm(10);
 for(unsigned charging=0;charging<2;charging++)for(unsigned connected=0;connected<2;connected++){
  auto s=seed();s.battery.charging=charging;s.battery.connected=connected;
  auto before=s.battery;
  for(unsigned n=0;n<4;n++){at(s,(1ULL+n)*1000000000ULL);assert(!submissions);}
  at(s,5000000000ULL);assert(submissions==1&&logs==1&&s.lowBattery.submitted);
  assert(s.lowBattery.samples==5&&s.lowBattery.admitted&&i9100_lpm_poweroff_request==2&&!i9100_lpm_poweroff_ready);
  assert(!memcmp(&before,&s.battery,sizeof(before)));
  s.battery.percent=100;s.battery.millivolts=4200;
  at(s,6000000000ULL);assert(submissions==1&&i9100_low_battery_retreat_diag[6]==3&&i9100_low_battery_retreat_diag[7]==3400);
 }
 for(unsigned mode=0;mode<8;mode++){
  auto s=seed();
  if(mode==0)s.lowBattery.enabled=0;if(mode==1)s.battery.valid=0;if(mode==2)s.battery.present=0;
  if(mode==3)s.battery.stop_uncertain=1;if(mode==4)s.battery.percent=4;
  if(mode==5)s.battery.millivolts=3401;if(mode==6)s.battery.millivolts=2499;if(mode==7)s.battery.ready=0;
  for(unsigned n=0;n<8;n++)at(s,(n+1ULL)*1000000000ULL);
  assert(!submissions&&!s.lowBattery.admitted);
 }
 {
  auto s=seed();for(unsigned n=0;n<1000;n++)at(s,1000000000ULL+n*1000000ULL);
  assert(!submissions&&s.lowBattery.samples==5);
  at(s,5000000000ULL);assert(!submissions); // stale gap starts a new window.
  at(s,6000000000ULL);at(s,7000000000ULL);at(s,8000000000ULL);at(s,9000000000ULL);at(s,10000000000ULL);
  assert(submissions==1);
 }
 {
  auto s=seed();for(unsigned n=0;n<4;n++)at(s,(n+1ULL)*1000000000ULL);
  current_ns=8000000000ULL;s.observeLowBattery();assert(!submissions&&!s.lowBattery.samples);
  at(s,9000000000ULL);at(s,10000000000ULL);at(s,9000000000ULL);assert(!s.lowBattery.samples);
  s.battery.valid=0;at(s,11000000000ULL);assert(!submissions);
 }
 for(unsigned pending=1;pending<=4;pending++){
  auto s=seed();i9100_lpm_poweroff_request=pending;
  for(unsigned n=0;n<8;n++)at(s,(n+1ULL)*1000000000ULL);
  assert(!submissions&&i9100_lpm_poweroff_request==pending);
 }
 {
  auto s=seed();i9100_lpm_poweroff_ready=0;
  for(unsigned n=0;n<8;n++)at(s,(n+1ULL)*1000000000ULL);
  assert(!submissions);
 }
 for(unsigned timer_fails=0;timer_fails<2;timer_fails++){
  auto s=seed();for(unsigned n=0;n<4;n++)at(s,(n+1ULL)*1000000000ULL);
  IOTimerEventSource timer;timer.fails=timer_fails;current_ns=5000000000ULL;
  IOS7I9100Power::tick(&s,&timer);
  assert(s.samples==1&&timer.arms==1&&submissions==!timer_fails&&s.stops==timer_fails);
 }
 for(int result=0;result<2;result++){
  auto s=seed();for(unsigned n=0;n<5;n++)at(s,(n+1ULL)*1000000000ULL);
  boot_result=result;i9100_lpm_poweroff_worker(nullptr,nullptr);
  assert(boots==1&&i9100_lpm_poweroff_request==(result?4U:3U));
  for(unsigned n=0;n<100;n++)at(s,(n+10ULL)*1000000000ULL);
  assert(submissions==1&&boots==1); // failed normal halt is never retried.
 }
 auto s=seed();for(unsigned n=0;n<5;n++)at(s,(n+1ULL)*1000000000ULL);
 FILE*f=fopen(argv[1],"wb");assert(f&&fwrite((const void*)i9100_low_battery_retreat_diag,1,72,f)==72);fclose(f);
 puts("PASS production low-cell retreat: dual thresholds, fresh five samples over four seconds, normal-halt worker once, no pending-request/timer-fault retry, untouched charging policy");
}
'''
(out/'check.cpp').write_text(code,encoding='utf8')
commands=(['clang++','-std=c++11','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(platform),str(out/'check.cpp'),'-o',str(out/'check')],
    [str(out/'check'),str(out/'record.bin')])
for n,args in enumerate(commands):
    result=subprocess.run(args,text=True,capture_output=True)
    (out/f'command-{n}.log').write_text(result.stdout+result.stderr,encoding='utf8')
    if result.returncode:print((result.stdout+result.stderr)[-2500:],file=sys.stderr);result.check_returncode()
    if result.stdout:print(result.stdout.strip())
path=root/'uniloader-S2I9100/scripts/i9100/inspect-charge-health.py'
spec=importlib.util.spec_from_file_location('low_cell_native_decoder',path);decoder=importlib.util.module_from_spec(spec);spec.loader.exec_module(decoder)
raw=(out/'record.bin').read_bytes();observed=decoder.decode_low_battery_retreat(raw,raw[:8])
assert observed['consistent'] and observed['admitted'] and observed['submitted_ns']==5000000000
assert not decoder.decode_low_battery_retreat(raw,b'\1\0\0\0\2\0\0\0')['consistent']
import struct
assert not decoder.decode_low_battery_retreat(struct.pack('<18I',1,*([0]*17)))['initialized']
for slot,value in ((0,2),(2,2),(3,5),(4,6),(6,101),(7,4500),(14,4),(15,3500),(16,4),(17,3),(8,4000000000)):
    bad=bytearray(raw);struct.pack_into('<I',bad,slot*4,value)
    try:decoder.decode_low_battery_retreat(bad)
    except ValueError:pass
    else:raise AssertionError('Invalid low-cell publication accepted '+str(slot))
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,observation=observed,
    source_sha256={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
        (source,platform/'i9100_low_battery.h',platform/'i9100_power.h',path)},
    limit='Actual Power callback/tick/normal BSD worker with timer/clock/boot facades; not a physical low-cell shutdown'),indent=2)+'\n',encoding='utf8')
print('EVIDENCE',out)
