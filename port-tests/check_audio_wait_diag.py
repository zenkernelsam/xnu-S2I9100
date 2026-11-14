#!/usr/bin/env python3
"""Execute production own-task wait capture and verify scope and lock-free reads."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('audio-wait-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32,user_addr_t;
struct proc {const char*p_comm;void*task;int p_pid;};typedef struct proc*proc_t;
typedef struct {uint32_t r[13],sp,lr,pc,cpsr;} arm_saved_state_t;
volatile uint32_t i9100_audio_property_runtime[4]={1,33,0x3333,0x20000};
volatile uint32_t i9100_audio_property_lock_history[97]={1,2,33,0x3333};
static unsigned irq,interrupts=1,preemption,enabled=1,thread=0x5555,calls;
static uint32_t runtime[16]={1,2,0x12000,0x4d555458,0x12068,1,0,1};
static unsigned current_thread(void){return thread;}
static int ml_at_interrupt_context(void){return irq;}static int ml_get_interrupts_enabled(void){return interrupts;}
static int get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,void*out,unsigned size){assert(!strcmp(n,"i9100xpcdiag")&&size==4);*(int*)out=enabled;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile unsigned*p){if(*p!=old)return 0;*p=value;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int copyin(user_addr_t,void*,unsigned);
'''+f'#include "{repo}/bsd/dev/arm/i9100_audio_wait_diag.h"\n'+r'''
static int copyin(user_addr_t at,void*out,unsigned n){
 calls++;assert(!i9100_audio_wait_guard);
 if(at==0x20000&&n==64){memcpy(out,runtime,n);return 0;}
 if(at==0x12000&&n==64){uint32_t m[16]={0x4d555458,0,0x2060,0,0x303,0x100,0,0,112,0};memcpy(out,m,n);return 0;}
 if(at==0x30020&&n==8){uint32_t pair[2]={0x30028,0x3169e85d};memcpy(out,pair,n);return 0;}
 if(at==0x30028&&n==8){uint32_t pair[2]={0,0x30affdd3};memcpy(out,pair,n);return 0;}
 return 14;
}
int main(int argc,char**argv){
 assert(argc==2);struct proc p={"mediaserverd",(void*)0x3333,33};arm_saved_state_t s={0};s.cpsr=0x10;s.r[0]=0x12000;s.r[7]=0x30020;s.sp=0x30000;s.pc=0x392cd0f8;s.lr=0x39245b3c;
 i9100_audio_wait_note(&p,301,0,&s);assert(i9100_audio_wait_diag[0]==1);
 volatile uint32_t*r=&i9100_audio_wait_diag[1];assert(r[0]==1&&r[4]==301&&r[10]==2&&!r[11]&&!r[12]);
 assert(r[28]==0x4d555458&&r[36]==112&&r[45]==2&&r[50]==0&&r[62]==0x3169e85d);
 unsigned saved=calls;i9100_audio_wait_note(&p,301,0,&s);assert(i9100_audio_wait_diag[0]==1&&calls==saved+1);
 for(unsigned n=0;n<7;n++){
  unsigned before=calls;irq=0;interrupts=1;preemption=0;enabled=1;p.p_pid=33;runtime[1]=2;
  switch(n){case 0:irq=1;break;case 1:interrupts=0;break;case 2:preemption=1;break;case 3:enabled=0;break;case 4:p.p_pid=34;break;case 5:runtime[1]=3;break;case 6:p.p_comm="SpringBoard";break;}
  i9100_audio_wait_note(&p,301,0,&s);assert(i9100_audio_wait_diag[0]==1);if(n!=5)assert(calls==before);
 }
 p.p_comm="mediaserverd";p.p_pid=33;runtime[1]=2;enabled=1;
 for(unsigned n=0;n<20;n++){thread=0x6000+n;i9100_audio_wait_note(&p,301,0,&s);}
 assert(i9100_audio_wait_diag[0]==8);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_audio_wait_diag,1,sizeof(i9100_audio_wait_diag),f);fclose(f);
 puts("PASS production audio wait identity/stage/scope/capacity and no copyin under observer guard; transport/atomics modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'audio-waits.bin')],check=True);print('EVIDENCE',out)
