#!/usr/bin/env python3
"""Compile production power-budget publishers; model the native API/lock boundary."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from datetime import datetime

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
        sys.executable, '-B', __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
repo = Path(__file__).resolve().parents[1]
headers = repo / 'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
power = (headers / 'IOS7I9100Power.cpp').read_text()
cpu = power[power.index('static void i9100_cpu_budget_sample('):power.index('\nclass IOS7I9100Power')]
display = (headers / 'IOS7LeoDisplay.cpp').read_text()
client = display[display.index('void IOS7LabFramebufferClient::recordDisplayPower('):
    display.index('\n/* Caller holds transactionLock.', display.index('void IOS7LabFramebufferClient::recordDisplayPower('))]
out = repo / 'out' / ('power-budget-check-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True, exist_ok=False)
prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "i9100_power_budget_diag.h"
#include "IOS7LeoGraphicsState.h"
#define BOARD_CONFIG_EXYNOS4210_I9100 1
using processor_t=void*;using host_t=void*;using processor_info_t=int32_t*;
using mach_msg_type_number_t=uint32_t;using kern_return_t=int32_t;
using processor_flavor_t=int32_t;
enum {KERN_SUCCESS=0,KERN_FAILURE=5,PROCESSOR_CPU_LOAD_INFO=2,PROCESSOR_CPU_LOAD_INFO_COUNT=4,
 CPU_STATE_USER=0,CPU_STATE_SYSTEM=1,CPU_STATE_IDLE=2,CPU_STATE_NICE=3};
struct processor_cpu_load_info_data_t {uint32_t cpu_ticks[4];};
static uint64_t now=1000000000ULL;
static uint64_t mach_absolute_time(){return now;}
static processor_t master_processor=(void*)0x81000000U;
static uint32_t hz_tick_interval=10000000,processor_avail_count=1;
static int precise_user_kernel_time;
static uint32_t native_ticks[4]={20,5,75,0},native_count=4,api_calls;
static int native_error;
static kern_return_t processor_info(processor_t processor,processor_flavor_t flavor,host_t *host,
 processor_info_t output,mach_msg_type_number_t *count){
 assert(processor==master_processor && flavor==2 && host && output && count && *count==4);
 ++api_calls;*count=native_count;
 if(native_error)return native_error;
 std::memcpy(output,native_ticks,sizeof(native_ticks));return 0;
}
static unsigned graphics_available=1,send_calls=100,send_success=100;
uint32_t ios7leo_graphics_snapshot(LeoGraphicsState *g){
 if(!graphics_available)return 0;
 std::memset(g,0,sizeof(*g));g->last_client=7;g->send_calls=send_calls;g->send_success=send_success;
 g->requested=g->enabled=g->port_present=g->callback_present=1;return 1;
}
static volatile uint32_t i9100_cpu_load_diag[I9100_POWER_BUDGET_WORDS]={1,0,20,16};
static volatile uint32_t i9100_display_client_power_diag[I9100_POWER_BUDGET_WORDS]={1,0,20,16};
struct MockLock {bool held=false,busy=false;};
static MockLock lock;
static void IOLockLock(MockLock *v){assert(v==&lock && !v->held);v->held=true;}
static bool IOLockTryLock(MockLock *v){assert(v==&lock);if(v->held||v->busy)return false;v->held=true;return true;}
static void IOLockUnlock(MockLock *v){assert(v==&lock && v->held);v->held=false;}
static unsigned leo_graphics_losses,leo_graphics_unknown,guard_calls;
static bool reject_guard,guard_held;
static void leo_graphics_drop(){++leo_graphics_losses;}
static bool leo_graphics_try(){assert(!lock.held && !guard_held);++guard_calls;
 if(reject_guard){++leo_graphics_losses;return false;}guard_held=true;return true;}
static void leo_graphics_unlock(){assert(guard_held && !lock.held);guard_held=false;}
#define IPC_PORT_VALID(p) ((p)!=0)
class IOS7LabFramebufferClient {public:
 MockLock *transactionLock=&lock;unsigned powerDiagEnabled=1;
 uint32_t graphicsClient=7,graphicsOwnerPID=61;
 bool vsyncRequested=true,vsyncEnabled=true,vsyncClosed=false;
 uint32_t vsyncPort=0x1234;uint64_t vsyncCallback=0x3456,vsyncGeneration=9,vsyncCount=100;
 void recordDisplayPower(uint32_t,int);
};
static void save(const std::string &name,volatile uint32_t *data){
 uint32_t copy[I9100_POWER_BUDGET_WORDS];for(unsigned i=0;i<I9100_POWER_BUDGET_WORDS;i++)copy[i]=data[i];
 FILE *f=std::fopen(name.c_str(),"wb");assert(f);
 assert(std::fwrite(copy,1,sizeof(copy),f)==sizeof(copy));assert(!std::fclose(f));
}
'''
suffix = r'''
int main(int argc,char **argv){
 assert(argc==2);std::string directory=argv[1];
 i9100_cpu_budget_sample(1);
 assert(i9100_cpu_load_diag[1]==1 && i9100_cpu_load_diag[4+4]==1 && api_calls==1);
 now=31000000000ULL;native_ticks[0]=620;native_ticks[1]=305;native_ticks[2]=2175;
 send_calls=1900;send_success=1800;i9100_cpu_budget_sample(30);
 save(directory+"/native-cpu.bin",i9100_cpu_load_diag);
 native_error=5;i9100_cpu_budget_sample(60);
 assert(i9100_cpu_load_diag[4+2*20+4]==0 && i9100_cpu_load_diag[4+2*20+5]==5);
 native_error=0;native_count=3;i9100_cpu_budget_sample(90);
 assert(i9100_cpu_load_diag[4+3*20+4]==0);
 unsigned calls=api_calls;master_processor=0;i9100_cpu_budget_sample(120);assert(api_calls==calls);
 master_processor=(void*)0x81000000U;hz_tick_interval=0;i9100_cpu_budget_sample(150);assert(api_calls==calls);
 hz_tick_interval=10000000;processor_avail_count=0;i9100_cpu_budget_sample(180);assert(api_calls==calls);
 processor_avail_count=1;native_count=4;graphics_available=0;i9100_cpu_budget_sample(210);
 assert(i9100_cpu_load_diag[4+7*20+4]==1 && i9100_cpu_load_diag[4+7*20+15]==0);
 IOS7LabFramebufferClient a;a.recordDisplayPower(0,0);
 assert(!lock.held && !guard_held && i9100_display_client_power_diag[1]==1);
 assert(i9100_display_client_power_diag[4+3]==0 && i9100_display_client_power_diag[4+6]==61);
 assert(a.vsyncRequested && a.vsyncEnabled && !a.vsyncClosed && a.vsyncCount==100);
 a.graphicsClient=8;a.graphicsOwnerPID=104;a.vsyncCount=(3ULL<<32)|7;a.recordDisplayPower(1,2);
 assert(i9100_display_client_power_diag[4+20+4]==2 && i9100_display_client_power_diag[4+20+15]==3);
 save(directory+"/native-display-clients.bin",i9100_display_client_power_diag);
 calls=guard_calls;a.powerDiagEnabled=0;a.recordDisplayPower(0,0);assert(guard_calls==calls);
 a.powerDiagEnabled=1;reject_guard=true;a.recordDisplayPower(0,0);
 assert(i9100_display_client_power_diag[1]==2 && !lock.held && !guard_held && leo_graphics_losses==1);
 reject_guard=false;lock.busy=true;calls=guard_calls;a.recordDisplayPower(0,0);
 assert(i9100_display_client_power_diag[1]==2 && guard_calls==calls && leo_graphics_losses==2);
 lock.busy=false;
 reject_guard=false;a.vsyncClosed=true;a.recordDisplayPower(2,0);
 assert(a.vsyncClosed && a.vsyncEnabled && a.vsyncCount==((3ULL<<32)|7));
 assert(i9100_display_client_power_diag[4+2*20+18]==2);
 uint32_t words[19];for(unsigned n=0;n<19;n++)words[n]=n+1000;
 volatile uint32_t wrapped[I9100_POWER_BUDGET_WORDS]={1,0xfffffffeU,20,16};
 i9100_power_budget_publish(wrapped,words);assert(wrapped[1]==0xffffffffU && wrapped[4+14*20]==0xffffffffU);
 i9100_power_budget_publish(wrapped,words);assert(wrapped[1]==1 && wrapped[4]==1 && wrapped[4+19]==1018);
 puts("PASS production native-API sampling, failed counts, no private offsets, caller identity, lock order, disabled/busy guards and sequence wrap");
}
'''
(out / 'check.cpp').write_text(prefix + cpu + '\n' + client + suffix)
for command, name in (
    (['clang++', '-std=c++11', '-O1', '-g', '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
      '-I'+str(headers), str(out/'check.cpp'), '-o', str(out/'check')], 'compile'),
    ([str(out/'check'), str(out)], 'result')):
    result = subprocess.run(command, text=True, capture_output=True)
    (out / (name+'.log')).write_text(result.stdout + result.stderr)
    result.check_returncode()
    if name=='result':print(result.stdout.strip())

path = repo.parent / 'uniloader-S2I9100/scripts/i9100/inspect-charge-health.py'
spec = importlib.util.spec_from_file_location('budget_reader', path)
reader = importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
raw = (out/'native-cpu.bin').read_bytes()
decoded = reader.decode_budget(raw, 'cpu')
window = decoded['intervals'][0]
assert window['usable'] and window['idle_percent']==70 and window['busy_percent']==30
assert not window['user_kernel_split_valid'] and window['graphics_send_calls_delta']==1800
clients = reader.decode_budget((out/'native-display-clients.bin').read_bytes(), 'display_clients')
assert [row['owner_pid'] for row in clients['samples']]==[61,104]
assert clients['samples'][1]['send_count']==(3<<32)|7

def changed(values):
    data=bytearray(raw)
    for offset,value in values:struct.pack_into('<I',data,offset,value)
    return reader.decode_budget(bytes(data),'cpu')

assert not changed([(16+80+4*4,0),(16+80+5*4,5)])['intervals'][0]['usable']
assert not changed([(16+80+1*4,1000000000),(16+80+2*4,0)])['intervals'][0]['usable']
assert not changed([(16+80+14*4,0x82000000)])['intervals'][0]['usable']
stale=changed([(16+80+8*4,20),(16+80+9*4,5),(16+80+10*4,75)])
assert not stale['intervals'][0]['usable']
wrap=changed([(16+8*4,0xfffffff4),(16+80+8*4,588)])
assert wrap['intervals'][0]['usable'] and wrap['intervals'][0]['idle_percent']==70
for offset,value in ((0,2),(8,19),(16+80,0xffffff00),(16+80+6*4,3),
        (16+80+14*4,0),(16+80+7*4,0),(16+80+7*4,33),(16+80+13*4,2)):
    try:changed([(offset,value)])
    except ValueError:pass
    else:raise AssertionError('Corrupt schema/native result accepted')
print('PASS actual publisher fixtures decode to 70% idle; failures, clocks, CPU identity and corruption do not become false idle')
(out/'decoded-cpu.json').write_text(json.dumps(decoded,indent=2)+'\n')
(out/'decoded-display-clients.json').write_text(json.dumps(clients,indent=2)+'\n')
print('EVIDENCE',out)
