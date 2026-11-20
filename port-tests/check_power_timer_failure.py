#!/usr/bin/env python3
"""Exercise the actual Power tick failure path with production charger policy.

Timer/IOKit and register callbacks are explicit models. Confirms that failed
rearm invalidates measurements and warm-reset records without claiming that
a failed hardware stop succeeded. No serial or device operation occurs.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import hashlib,json,subprocess
repo=Path(__file__).resolve().parents[1];platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
source=platform/'IOS7I9100Power.cpp';text=source.read_text(encoding='utf-8')
start=text.index('void IOS7I9100Power::tick(');brace=text.index('{',start);end=brace+1;depth=1
while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
tick=text[start:end];out=repo/'out'/('power-timer-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "i9100_charge_epoch.h"
using OSObject=void;
enum {kIOReturnSuccess=0};
struct IOTimerEventSource {bool fails=false;unsigned arms=0;int setTimeoutMS(unsigned ms){assert(ms==1000);arms++;return fails?7:0;}};
#define OSDynamicCast(type,owner) static_cast<type*>(owner)
static unsigned logs,stop_confirmed,last_reason;
static uint32_t i9100_power_charging,i9100_power_valid,i9100_power_stop_uncertain;
static int i9100_power_reason;
static volatile uint32_t i9100_charge_epoch_live[32];
static uint64_t mach_absolute_time(){return 90000000000ULL;}
static void IOLog(const char *format,...){
 va_list list;char line[512];va_start(list,format);vsnprintf(line,sizeof(line),format,list);va_end(list);
 logs++;assert(strstr(line,"supervision unavailable"));
 assert(strstr(line,stop_confirmed?"stop_confirmed=1":"stop_confirmed=0"));
}
struct Model {uint8_t registers[256]={};bool sub=true,stop_stuck=false;unsigned disable_calls=0;};
static int read(void *c,unsigned r,uint8_t *v){*v=((Model*)c)->registers[r];return 1;}
static int write(void *c,unsigned r,uint8_t v){((Model*)c)->registers[r]=v;return 1;}
static int disable(void *c,unsigned en){auto m=(Model*)c;assert(en==0);m->disable_calls++;if(m->stop_stuck)return 0;m->sub=false;return 1;}
struct IOS7I9100Power {
 i9100_power_state battery={};unsigned polls=17,samples=0,notifications=0;bool isCharging=true,validProperty=true;
 void sample(){samples++;polls++;}
 void setIsCharging(bool value){isCharging=value;}
 void setProperty(const char *key,bool value){assert(!strcmp(key,"MeasurementsValid"));validProperty=value;}
 void updateStatus(){notifications++;}
 static void tick(OSObject*,IOTimerEventSource*);
};
'''+tick+r'''
int main(){
 alarm(10);
 for(unsigned fails=0;fails<2;fails++)for(unsigned stuck=0;stuck<2;stuck++){
  Model m;IOS7I9100Power owner;owner.battery.ready=owner.battery.valid=owner.battery.enabled=owner.battery.charging=1;
  owner.battery.revision=14;owner.battery.use_sub=1;owner.battery.present=owner.battery.connected=1;
  owner.battery.percent=50;owner.battery.millivolts=4000;owner.battery.temperature_deci_c=250;
  owner.battery.charge_start=1000000000ULL;owner.battery.io.cookie=&m;
  owner.battery.io.pmic_read=read;owner.battery.io.pmic_write=write;owner.battery.io.sub_enable=disable;
  m.stop_stuck=stuck;IOTimerEventSource timer;timer.fails=fails;
  I9100ChargeEpoch record;i9100_charge_epoch_encode(&record,&owner.battery,80000000000ULL,17,1,900000000ULL);
  i9100_charge_epoch_publish(i9100_charge_epoch_live,&record);
  i9100_power_charging=i9100_power_valid=1;i9100_power_stop_uncertain=0;i9100_power_reason=0;logs=0;
  stop_confirmed=!stuck;IOS7I9100Power::tick(&owner,&timer);
  assert(owner.samples==1&&timer.arms==1&&owner.battery.charge_start==1000000000ULL);
  if(fails){
   assert(logs==1&&m.disable_calls==1&&!owner.battery.valid&&!i9100_power_valid&&!owner.isCharging&&!owner.validProperty);
   assert(i9100_power_reason==I9100_POWER_IO&&owner.battery.reason==I9100_POWER_IO&&owner.notifications==1);
   assert(i9100_power_stop_uncertain==stuck&&owner.battery.stop_uncertain==stuck&&m.sub==!!stuck);
   assert(!i9100_charge_epoch_read(i9100_charge_epoch_live,&record));
  }else{
   assert(!logs&&!m.disable_calls&&owner.validProperty&&owner.battery.valid);
   assert(i9100_charge_epoch_read(i9100_charge_epoch_live,&record)&&record.flags==3);
  }
 }
 IOTimerEventSource timer;IOS7I9100Power::tick(nullptr,&timer);assert(!timer.arms);
 puts("PASS actual Power tick: rearm failure invalidates live measurement and epoch, distinguishes failed disable readback, preserves deadline and original successful timer path");
}
'''
code=code.replace('static unsigned logs,stop_confirmed,last_reason;','static unsigned logs,stop_confirmed;')
(out/'check.cpp').write_text(code,encoding='utf-8')
commands=(['clang','-std=c99','-O1','-g','-fno-pie','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    '-I'+str(platform),'-c',str(platform/'i9100_power.c'),'-o',str(out/'policy.o')],
    ['clang++','-std=c++11','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    '-I'+str(platform),str(out/'check.cpp'),str(out/'policy.o'),'-o',str(out/'check')], [str(out/'check')])
for index,cmd in enumerate(commands):
    result=subprocess.run(cmd,capture_output=True,text=True)
    (out/f'command-{index}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:print((result.stdout+result.stderr)[-2500:],file=sys.stderr);result.check_returncode()
    if result.stdout:print(result.stdout.strip(),flush=True)
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,
    source_sha256={str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    (source,platform/'i9100_power.c',platform/'i9100_power.h',platform/'i9100_charge_epoch.h')},
    limit='Actual production tick and portable policy, timer/IOKit/register models; no live timer failure or hardware charging observation'),indent=2)+'\n',encoding='utf-8')
print('EVIDENCE',out,flush=True)
