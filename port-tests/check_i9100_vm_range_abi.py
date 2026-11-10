#!/usr/bin/env python3
"""Run the actual ARM trap routers/helpers against iOS6 saved-register wires.

Artifacts are kept in a fresh out directory; no automatic deletion. VM stubs
check only backend arguments/ownership/error propagation, not GUI success.
"""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,__file__], dict(os.environ,I9100_CPU_CAPPED='1'))
repo = Path(__file__).resolve().parents[1]
source = (repo / 'osfmk/ipc/mach_kernelrpc.c').read_text()
old = subprocess.run(['git','show','6642a8b:osfmk/ipc/mach_kernelrpc.c'], cwd=repo,
                     text=True,capture_output=True,check=True).stdout


def function(text, name, prefix='int '):
    start = text.index(name+'(')
    begin = text.index('{', start)
    depth = 1
    end = begin+1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return prefix + text[start:end] + '\n'


prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ios7lab_vm_range_wire.h"
#define BOARD_CONFIG_QSD8250_LEO 0
#define BOARD_CONFIG_EXYNOS3475_J1XLTE 0
#define BOARD_CONFIG_EXYNOS4210_I9100 1
typedef uint32_t mach_vm_address_t;
typedef uint32_t mach_vm_size_t;
typedef int boolean_t;
typedef int vm_prot_t;
typedef struct task {void *map;} *task_t;
enum {KERN_SUCCESS=0,KERN_INVALID_ARGUMENT=4,MACH_SEND_INVALID_DEST=0x10000003};
static int map_token, other_token;
static struct task own={&map_token},foreign={&other_token};
static unsigned backend_calls,releases,record_called,backend_address,backend_size;
static unsigned backend_max,backend_prot;
static int backend_result;
static task_t port_name_to_task(uint32_t target){return target==1?&own:target==2?&foreign:0;}
static task_t current_task(void){return &own;}
static void task_deallocate(task_t task){assert(task);++releases;}
static int mach_vm_deallocate(void *map,uint32_t address,uint32_t size){
 assert(map==&map_token);++backend_calls;backend_address=address;backend_size=size;return backend_result;
}
static int mach_vm_protect(void *map,uint32_t address,uint32_t size,int maximum,int protection){
 assert(map==&map_token);++backend_calls;backend_address=address;backend_size=size;
 backend_max=maximum;backend_prot=protection;return backend_result;
}
static void ios7leo_vm_range_record(unsigned protect,const struct ios7lab_vm_range_wire *wire,
 unsigned prot,int result,int called){(void)protect;(void)wire;(void)prot;(void)result;record_called=called;}
struct _kernelrpc_mach_vm_deallocate_args {uint32_t target,address,size;};
struct _kernelrpc_mach_vm_protect_args {uint32_t target,address,size;int set_maximum,new_protection;};
static void reset(int result){backend_calls=releases=record_called=backend_address=backend_size=backend_max=backend_prot=0;backend_result=result;}
'''
names = ['_kernelrpc_mach_vm_deallocate_trap','_kernelrpc_mach_vm_protect_trap']
body = ''.join(function(source, n, 'static int ') for n in
               ['ios7leo_vm_deallocate64','ios7leo_vm_protect64'])
for name in names:
    body += function(source, name)
    body += function(old, name).replace(name+'(', 'old'+name+'(')
tests = r'''
int main(void){
 unsigned cases=0;
 union {struct _kernelrpc_mach_vm_deallocate_args native;struct ios7lab_vm_range_wire wire;} d;
 union {struct _kernelrpc_mach_vm_protect_args native;struct ios7lab_vm_protect_wire wire;} p;
 d.wire=(struct ios7lab_vm_range_wire){1,0x052f1000,0,4096,0};
 reset(0);assert(old_kernelrpc_mach_vm_deallocate_trap(&d.native)==0);
 assert(backend_calls==1 && backend_size==0);++cases;
 p.wire=(struct ios7lab_vm_protect_wire){1,0x052f1000,0,4096,0,1,5};
 reset(0);assert(old_kernelrpc_mach_vm_protect_trap(&p.native)==0);
 assert(backend_size==0 && backend_max==4096 && backend_prot==0);++cases;
 uint32_t addresses[]={0,0x1000,0x052f1000,0x3fe00000};
 uint32_t sizes[]={0,1,4096,8192};
 for(unsigned a=0;a<4;a++)for(unsigned z=0;z<4;z++){
  d.wire=(struct ios7lab_vm_range_wire){1,addresses[a],0,sizes[z],0};
  reset(0);assert(_kernelrpc_mach_vm_deallocate_trap(&d.native)==0);
  assert(backend_calls==1 && releases==1 && record_called==1);
  assert(backend_address==addresses[a] && backend_size==sizes[z]);++cases;
  p.wire=(struct ios7lab_vm_protect_wire){1,addresses[a],0,sizes[z],0,1,5};
  reset(0);assert(_kernelrpc_mach_vm_protect_trap(&p.native)==0);
  assert(backend_calls==1 && releases==1 && backend_address==addresses[a] && backend_size==sizes[z]);
  assert(backend_max==1 && backend_prot==5);++cases;
 }
 for(unsigned high=0;high<2;high++){
  d.wire=(struct ios7lab_vm_range_wire){1,0x1000,high==0,4096,high==1};
  reset(0);assert(_kernelrpc_mach_vm_deallocate_trap(&d.native)==4);
  assert(!backend_calls && !record_called && releases==1);++cases;
  p.wire=(struct ios7lab_vm_protect_wire){1,0x1000,high==0,4096,high==1,0,7};
  reset(0);assert(_kernelrpc_mach_vm_protect_trap(&p.native)==4);
  assert(!backend_calls && !record_called && releases==1);++cases;
 }
 for(unsigned target=0;target<=2;target+=2){
  d.wire=(struct ios7lab_vm_range_wire){target,0x1000,0,4096,0};
  reset(0);assert(_kernelrpc_mach_vm_deallocate_trap(&d.native)==MACH_SEND_INVALID_DEST);
  assert(!backend_calls && releases==(target!=0));++cases;
  p.wire=(struct ios7lab_vm_protect_wire){target,0x1000,0,4096,0,0,5};
  reset(0);assert(_kernelrpc_mach_vm_protect_trap(&p.native)==MACH_SEND_INVALID_DEST);
  assert(!backend_calls && releases==(target!=0));++cases;
 }
 for(int result=1;result<=5;result++){
  d.wire=(struct ios7lab_vm_range_wire){1,0x1000,0,4096,0};reset(result);
  assert(_kernelrpc_mach_vm_deallocate_trap(&d.native)==result && backend_calls==1 && releases==1);++cases;
  p.wire=(struct ios7lab_vm_protect_wire){1,0x1000,0,4096,0,0,5};reset(result);
  assert(_kernelrpc_mach_vm_protect_trap(&p.native)==result && backend_calls==1 && releases==1);++cases;
 }
 printf("PASS %u production ARM VM trap wire cases; old zero-byte control, ownership/high-word/backend results; ASan/UBSan\n",cases);
}
'''
out = repo / 'out' / ('vm-range-abi-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True,exist_ok=False)
code = out/'test.c'
code.write_text(prefix+body+tests)
binary = out/'test'
command = ['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
           '-fno-strict-aliasing','-I'+str(repo/'osfmk/ipc'),str(code),'-o',str(binary)]
subprocess.run(command,check=True)
result = subprocess.run([str(binary)],text=True,capture_output=True,check=True)
(out/'result.txt').write_text(result.stdout+result.stderr)
print(result.stdout,end='')
print('Artifacts:',out)
