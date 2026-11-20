#!/usr/bin/env python3
"""Exercise production CDC log cadence and full snapshot without a phone.

USB stats and the clock are explicit facades. The actual Power reporter and
cadence/publication header run under ASan/UBSan, with every raw word retained.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import hashlib,importlib.util,json,re,subprocess
repo=Path(__file__).resolve().parents[1]
platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
power=platform/'IOS7I9100Power.cpp';text=power.read_text(encoding='utf-8')
out=repo/'out'/('cdc-log-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
start=text.index('static void i9100_power_cdc_report(void)');begin=text.index('{',start);end=begin+1;depth=1
while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
reporter=text[start:end]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "i9100_cdc_log.h"
static uint32_t raw[I9100_USB_STATS_WORDS];
static uint64_t ticks=1000000000ULL;
static unsigned stat_reads,log_calls;
static struct i9100_cdc_log_state i9100_cdc_log_state;
static volatile uint32_t i9100_cdc_liveness_diag[I9100_CDC_LOG_DIAG_WORDS]={1,0,27};
void i9100_usbcdc_stats(unsigned *out){stat_reads++;memcpy(out,raw,sizeof(raw));}
static uint64_t mach_absolute_time(void){return ticks;}
static void IOLog(const char *fmt,...){
 va_list ap;char line[512];va_start(ap,fmt);int n=vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
 assert(n>0&&n<(int)sizeof(line)&&!strncmp(line,"I9100 CDC m=",12));log_calls++;
}
'''+reporter+r'''
static void reset(void){
 memset(raw,0,sizeof(raw));memset(&i9100_cdc_log_state,0,sizeof(i9100_cdc_log_state));
 memset((void*)i9100_cdc_liveness_diag,0,sizeof(i9100_cdc_liveness_diag));
 raw[0]=2;raw[2]=1;raw[8]=raw[9]=raw[21]=raw[26]=1;
 raw[15]=0x00488200;raw[18]=0x80088200;ticks=1000000000ULL;stat_reads=log_calls=0;
}
static uint32_t sample(void){
 i9100_power_cdc_report();
 assert(!(i9100_cdc_liveness_diag[1]&1)&&i9100_cdc_liveness_diag[0]==1&&i9100_cdc_liveness_diag[2]==27);
 for(unsigned n=0;n<27;n++)assert(i9100_cdc_liveness_diag[8+n]==raw[n]);
 return i9100_cdc_liveness_diag[5]&255;
}
int main(int argc,char **argv){
 assert(argc==2);alarm(10);reset();
 assert(sample()&I9CL_FIRST);
 for(unsigned second=1;second<=60;second++){
  ticks+=1000000000ULL;raw[2]+=1000;raw[3]^=0x100;raw[4]++;raw[5]+=64;raw[6]+=64;
  raw[7]^=1;raw[12]++;raw[17]=second%3;raw[19]=second&1?512:0x80200;raw[20]++;
  raw[15]^=1U<<16;raw[18]^=1U<<16;raw[24]++;
  unsigned before=log_calls;uint32_t reason=sample();
  assert(log_calls==before+(second%15==0));assert(reason==(second%15==0?I9CL_PERIOD:0));
 }
 assert(log_calls==5&&stat_reads==61&&i9100_cdc_log_state.suppressed==56);
 // Short DATA flights and changing counters do not manufacture a fault.
 raw[10]=1;raw[11]=64;raw[17]=0;ticks+=1000000000ULL;raw[2]++;assert(!sample());
 raw[17]=3;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_FAULT);
 assert(i9100_cdc_log_state.faults&I9CF_REPLY);
 raw[10]=raw[11]=0;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_FAULT);
 // A frozen poll counter is only considered after three real seconds.
 ticks+=1000000000ULL;assert(!sample());ticks+=1000000000ULL;assert(!sample());
 ticks+=1000000000ULL;assert(sample()&I9CL_FAULT);assert(i9100_cdc_log_state.faults&I9CF_POLL);
 ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_FAULT);assert(!i9100_cdc_log_state.faults);
 raw[8]=0;ticks+=1000000000ULL;assert(sample()&I9CL_STATE);
 raw[8]=1;raw[9]=0;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_STATE);
 raw[9]=1;raw[21]=0;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_STATE);
 raw[21]=1;raw[23]=8;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_FAULT);
 raw[23]=0;raw[25]=1;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_FAULT);
 raw[25]=0;raw[18]|=1U<<21;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_FAULT);
 raw[18]&=~(1U<<21);raw[16]++;ticks+=1000000000ULL;raw[2]++;assert(sample()&I9CL_STATE);
 ticks=5;raw[2]++;assert(sample()&I9CL_CLOCK);
 unsigned before=log_calls;
 for(unsigned n=0;n<1000;n++){ticks+=1000;raw[2]++;assert(!sample());}
 assert(log_calls==before); // Dense callbacks cannot shorten the heartbeat.
 // Counter wraps are progress, not a reason to spam a healthy link.
 reset();raw[2]=UINT32_MAX;assert(sample());ticks+=1000000000ULL;raw[2]=0;assert(!sample());
 // Preserve arbitrary raw flags even though only semantic transitions log.
 reset();sample();
 char path[4096];snprintf(path,sizeof(path),"%s/snapshot.bin",argv[1]);FILE*f=fopen(path,"wb");assert(f);
 uint32_t snapshot[I9100_CDC_LOG_DIAG_WORDS];for(unsigned n=0;n<I9100_CDC_LOG_DIAG_WORDS;n++)snapshot[n]=i9100_cdc_liveness_diag[n];
 assert(fwrite(snapshot,1,sizeof(snapshot),f)==sizeof(snapshot));fclose(f);
 puts("PASS production Power CDC report: busy/traffic/DATA/idle rearm suppression, real15s heartbeat, connection/fault/clock transitions, three-second stalls and every raw register retained");
}
'''
(out/'check.c').write_text(code,encoding='utf-8')
for index,cmd in enumerate((['clang','-std=c99','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(platform),str(out/'check.c'),'-o',str(out/'check')],
    [str(out/'check'),str(out)])):
    result=subprocess.run(cmd,capture_output=True,text=True)
    (out/f'command-{index}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:print((result.stdout+result.stderr)[-2500:],file=sys.stderr);result.check_returncode()
    if result.stdout:print(result.stdout.strip(),flush=True)
spec=importlib.util.spec_from_file_location('cdc_host',repo.parent/'uniloader-S2I9100/scripts/i9100/inspect-charge-health.py')
host=importlib.util.module_from_spec(spec);spec.loader.exec_module(host)
raw=(out/'snapshot.bin').read_bytes();decoded=host.decode_cdc_liveness(raw,raw[:8])
assert decoded['consistent'] and decoded['stats']['configured']==1 and len(decoded['stats'])==27
import struct
version,sequence=struct.unpack_from('<2I',raw)
assert not host.decode_cdc_liveness(raw,struct.pack('<2I',version,sequence+2))['consistent']
(out/'snapshot.json').write_text(json.dumps(decoded,indent=2)+'\n',encoding='utf-8')
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,
    source_sha256={str(path.relative_to(repo)):hashlib.sha256(path.read_bytes()).hexdigest() for path in (power,platform/'i9100_cdc_log.h')},
    host_sha256=hashlib.sha256((repo.parent/'uniloader-S2I9100/scripts/i9100/inspect-charge-health.py').read_bytes()).hexdigest(),
    limit='Actual reporter/cadence/publication and host decoder; hardware stats/clock/IOLog modeled, no phone energy saving proven'),indent=2)+'\n',encoding='utf-8')
print('EVIDENCE',out,flush=True)
