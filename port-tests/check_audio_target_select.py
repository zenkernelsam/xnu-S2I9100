#!/usr/bin/env python3
"""Execute the production target selector with identity and publication controls."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('audio-target-check-'+datetime.now().strftime('%Y%m%d-%H%M%S-%f'));out.mkdir()
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t user_addr_t;
volatile uint32_t i9100_audio_property_lock_diag[12],i9100_audio_property_runtime[4];
static unsigned pid=33,owner=0x3333,irq,interrupts=1,preemption,xpc=1,repair=1,abi=6,copies,failcopy,morph;
static uint32_t words[16];
int proc_selfpid(void){return pid;}
static void*current_task(void){return (void*)(uintptr_t)owner;}
static int ml_at_interrupt_context(void){return irq;}
static int ml_get_interrupts_enabled(void){return interrupts;}
static int get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,void*out,unsigned bytes){assert(bytes==4);*(int*)out=!strcmp(n,"i9100xpcdiag")?xpc:!strcmp(n,"i9100audiolock")?repair:abi;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile uint32_t*p){if(*p!=old)return 0;*p=value;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int copyin(user_addr_t,void*,unsigned);
''' + f'#include "{repo}/bsd/dev/arm/i9100_audio_target_select.h"\n' + r'''
static int copyin(user_addr_t at,void*out,unsigned size){
 assert(!irq&&interrupts&&!preemption&&i9100_audio_observation_target[1]==0&&at==0x20000&&size==64);
 copies++;if(failcopy)return 14;memcpy(out,words,size);
 if(morph){i9100_audio_property_runtime[1]=34;i9100_audio_property_lock_diag[1]=34;}
 return 0;
}
static void reset(void){
 memset((void*)i9100_audio_observation_target,0,sizeof(i9100_audio_observation_target));i9100_audio_observation_target[0]=1;
 pid=33;owner=0x3333;irq=preemption=copies=failcopy=morph=0;interrupts=xpc=repair=1;abi=6;
 memset((void*)i9100_audio_property_lock_diag,0,sizeof(i9100_audio_property_lock_diag));
 i9100_audio_property_lock_diag[0]=2;i9100_audio_property_lock_diag[1]=pid;i9100_audio_property_lock_diag[2]=owner;i9100_audio_property_lock_diag[3]=0x100000;
 i9100_audio_property_runtime[0]=1;i9100_audio_property_runtime[1]=pid;i9100_audio_property_runtime[2]=owner;i9100_audio_property_runtime[3]=0x20000;
 memset(words,0,sizeof(words));words[0]=1;words[1]=2;
}
int main(int argc,char**argv){
 assert(argc==2);reset();words[0]=words[1]=0;assert(!i9100_audio_select_task(pid,owner)&&!i9100_audio_observation_target[1]);
 pid=34;owner=0x4444;i9100_audio_property_lock_diag[1]=pid;i9100_audio_property_lock_diag[2]=owner;
 i9100_audio_property_runtime[1]=pid;i9100_audio_property_runtime[2]=owner;words[0]=1;words[1]=2;
 assert(i9100_audio_select_task(pid,owner)&&i9100_audio_observation_target[1]==2&&i9100_audio_observation_target[2]==34&&i9100_audio_observation_target[3]==owner);
 unsigned before=copies;i9100_audio_property_lock_diag[1]=99;i9100_audio_property_runtime[1]=99;
 assert(i9100_audio_select_task(pid,owner)&&copies==before);pid=35;assert(!i9100_audio_select_task(pid,owner)&&copies==before&&i9100_audio_observation_target[2]==34);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_audio_observation_target,1,32,f);fclose(f);
 for(unsigned n=0;n<17;n++){
  reset();switch(n){case 0:irq=1;break;case 1:interrupts=0;break;case 2:preemption=1;break;case 3:xpc=0;break;case 4:repair=0;break;case 5:abi=7;break;
   case 6:i9100_audio_property_lock_diag[0]=3;break;case 7:i9100_audio_property_runtime[1]=34;break;case 8:i9100_audio_property_runtime[3]=1;break;
   case 9:i9100_audio_property_lock_diag[3]=1;break;case 10:words[0]=0;break;case 11:words[1]=1;break;case 12:words[1]=5;break;case 13:failcopy=1;break;
   case 14:morph=1;break;case 15:owner=0x5555;break;case 16:pid=34;break;}
  assert(!i9100_audio_select_task(pid,owner)&&i9100_audio_observation_target[1]==0);
 }
 reset();i9100_audio_observation_target[1]=1;assert(!i9100_audio_select_task(pid,owner)&&!copies);
 puts("PASS production first-executed callback selection, early skip, fixed shared identity, no copy during publication, bad context/metadata and copy-fault identity changes");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'selected.bin')],check=True)
print('EVIDENCE',out)
