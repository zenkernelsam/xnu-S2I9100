#!/usr/bin/env python3
"""Production early-exit retention versus later executed-callback capture."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('media-retention-check-'+datetime.now().strftime('%Y%m%d-%H%M%S-%f'));out.mkdir()
body=(repo/'bsd/kern/i9100_media_kill_payload.h').read_text()
body='\n'.join(x for x in body.splitlines() if not x.startswith('#include'))
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32,user_addr_t;
struct proc {const char*p_comm;unsigned p_pid;void*task;};typedef struct proc*proc_t;
typedef struct {uint32_t r[13],sp,lr,pc,cpsr;} arm_saved_state_t;
#define SIGKILL 9
volatile uint32_t i9100_audio_property_runtime[4],i9100_audio_property_lock_diag[12],i9100_audio_observation_target[8]={1};
static unsigned irq,interrupts=1,preemption,enabled=1,copies,threads_calls,selected_pid;
static int ml_at_interrupt_context(void){return irq;}static int ml_get_interrupts_enabled(void){return interrupts;}static int get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,void*out,unsigned size){assert(!strcmp(n,"i9100xpcdiag")&&size==4);*(int*)out=enabled;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile unsigned*p){if(*p!=old)return 0;*p=value;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
void i9100_media_threads_note(unsigned pid,unsigned slide){threads_calls++;assert(slide==0x100000);if(i9100_audio_observation_target[1]==2&&i9100_audio_observation_target[2]==pid)selected_pid=pid;}
static int copyin(user_addr_t at,void*out,unsigned size){
 assert(!irq&&interrupts&&!preemption);copies++;
 if(at==0x30020&&size==8){uint32_t pair[2]={0,0x317a426f};memcpy(out,pair,8);return 0;}
 if((at==0x40000||at==0x50000)&&size==512){memset(out,at==0x40000?0x33:0x44,size);return 0;}
 if(at==0x20000&&size==64){uint32_t words[16]={2,2,0x12000,0x4d555458,0x12068,1,0,1};memcpy(out,words,size);return 0;}
 if(at==0x3179e848&&size==8){uint32_t words[2]={0xf000f8df,0x39001};memcpy(out,words,size);return 0;}
 return 14;
}
'''
suffix=r'''
int main(int argc,char**argv){
 assert(argc==3);struct proc p={"mediaserverd",33,(void*)0x3333};arm_saved_state_t s={0};
 s.pc=0x393cc838;s.lr=0x317a426f;s.cpsr=0x10;s.r[0]=33;s.r[1]=9;s.sp=0x30000;s.r[7]=0x30020;s.r[5]=0x40000;
 i9100_media_kill_note(&p,&s,9,0);assert(i9100_media_kill_payload[0]==1&&i9100_media_kill_payload[1]==33&&!i9100_media_audio_runtime[0]&&threads_calls==1);
 uint32_t earliest[192];memcpy(earliest,(const void*)i9100_media_kill_payload,sizeof(earliest));
 p.p_pid=34;p.task=(void*)0x4444;s.r[0]=34;s.r[5]=0x50000;
 i9100_audio_observation_target[1]=2;i9100_audio_observation_target[2]=34;i9100_audio_observation_target[3]=0x4444;i9100_audio_observation_target[4]=0x100000;i9100_audio_observation_target[5]=0x20000;
 i9100_media_kill_note(&p,&s,9,0);
 assert(selected_pid==34&&threads_calls==2&&i9100_media_audio_runtime[0]==1&&i9100_media_audio_runtime[1]==34&&i9100_media_audio_runtime[8]==2);
 assert(!memcmp(earliest,(const void*)i9100_media_kill_payload,sizeof(earliest)));
 unsigned before=copies;i9100_media_kill_note(&p,&s,9,0);assert(copies==before&&threads_calls==3);
 p.p_pid=35;p.task=(void*)0x5555;i9100_media_kill_note(&p,&s,9,0);assert(copies==before&&i9100_media_audio_runtime[1]==34);
 unsigned calls=threads_calls;s.pc+=4;i9100_media_kill_note(&p,&s,9,0);assert(threads_calls==calls);s.pc-=4;
 for(unsigned n=0;n<6;n++){irq=0;interrupts=1;preemption=0;enabled=1;unsigned signum=9,flavor=0;
  if(n==0)irq=1;if(n==1)interrupts=0;if(n==2)preemption=1;if(n==3)enabled=0;if(n==4)signum=15;if(n==5)flavor=1;
  i9100_media_kill_note(&p,&s,signum,flavor);assert(threads_calls==calls&&copies==before);
 }
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite(earliest,1,sizeof(earliest),f);fclose(f);
 f=fopen(argv[2],"wb");assert(f);fwrite((const void*)i9100_media_audio_runtime,1,sizeof(i9100_media_audio_runtime),f);fclose(f);
 puts("PASS original first failure retained, later selected callback captured once, foreign/later tasks and unsafe contexts excluded");
}
'''
(out/'check.c').write_text(prefix+body+suffix)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'earliest.bin'),str(out/'selected-runtime.bin')],check=True)
print('EVIDENCE',out)
