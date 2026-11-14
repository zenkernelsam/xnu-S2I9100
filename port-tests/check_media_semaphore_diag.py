#!/usr/bin/env python3
"""Exercise the production scoped observer; scheduling and IPC are modeled."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                         sys.executable, '-B', __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess
repo = Path(__file__).resolve().parents[1]
out = repo / 'out' / ('media-semaphore-check-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32,user_addr_t;typedef int32_t SInt32;
typedef struct {uint32_t r[13],sp,lr,pc,cpsr;} arm_saved_state_t;
struct task{unsigned value;};typedef struct task*task_t;
struct thread{uint64_t tid;struct{arm_saved_state_t*uss;}machine;};typedef struct thread*thread_t;
static struct task task,other,kernel;static task_t now_task=&task,kernel_task=&kernel;
static arm_saved_state_t state;static struct thread thread={247,{&state}};
static task_t current_task(void){return now_task;}static thread_t current_thread(void){return &thread;}
static uint64_t thread_tid(thread_t t){return t->tid;}
static unsigned irq,interrupts=1,preemption,pid=33,pidreads,userreads,badframe;unsigned int real_ncpus=1;
static int ml_at_interrupt_context(void){return irq;}static int ml_get_interrupts_enabled(void){return interrupts;}
static int get_preemption_level(void){return preemption;}static int proc_selfpid(void){pidreads++;return pid;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile unsigned*p){if(*p!=old)return 0;*p=value;return 1;}
static int OSIncrementAtomic(volatile SInt32*p){return ++*p;}
#define OSMemoryBarrier() __sync_synchronize()
volatile uint32_t i9100_audio_property_lock_history[97];
static int copyin(user_addr_t,void*,unsigned);
''' + f'#include "{repo}/pexpert/arm/i9100_media_semaphore_diag.h"\n' + r'''
static int copyin(user_addr_t at,void*out,unsigned n){
 assert(!i9100_media_semaphore_guard&&!irq&&interrupts&&!preemption);userreads++;
 if(at==0x30020&&n==8){if(badframe)return 14;uint32_t pair[2]={0,0x31756a17};memcpy(out,pair,n);return 0;}
 if(at==0x39594dd0&&n==4){uint32_t q=0x25000;memcpy(out,&q,n);return 0;}
 if(at==0x25000&&n==256){memset(out,0,n);strcpy((char*)out+56,"AudioControl");return 0;}
 return 14;
}
static void reset(void){
 memset((void*)i9100_media_semaphore_diag,0,sizeof(i9100_media_semaphore_diag));
 memset((void*)i9100_media_dispatch_wait,0,sizeof(i9100_media_dispatch_wait));
 i9100_media_semaphore_claim=i9100_media_semaphore_guard=i9100_media_semaphore_target=0;
 irq=preemption=pidreads=userreads=badframe=0;interrupts=real_ncpus=1;pid=33;now_task=&task;
 thread.machine.uss=&state;state=(arm_saved_state_t){0};state.pc=0x393bcf04;state.cpsr=0x10;
 state.sp=0x30000;state.r[7]=0x30020;
 i9100_audio_property_lock_history[0]=1;i9100_audio_property_lock_history[2]=33;
 i9100_audio_property_lock_history[3]=(unsigned)(uintptr_t)&task;i9100_audio_property_lock_history[4]=0x100000;
}
static volatile uint32_t*row(unsigned n){return &i9100_media_semaphore_diag[12+(n-1)*16];}
int main(int argc,char**argv){
 assert(argc==3);reset();i9100_media_semaphore_begin(0x81234567,0xa203);
 assert(i9100_media_semaphore_diag[0]==1&&row(1)[1]==1&&row(1)[7]==0x81234567&&row(1)[8]==0xa203);
 assert(i9100_media_dispatch_wait[0]==1&&i9100_media_dispatch_wait[5]==0x39594dd0&&i9100_media_dispatch_wait[9]==256);
 assert(!strcmp((const char*)&i9100_media_dispatch_wait[16+14],"AudioControl"));
 unsigned copies=userreads,pids=pidreads;irq=preemption=1;interrupts=0;
 i9100_media_semaphore_record(2,0x81234567,UINT32_MAX,1,99,0,0);
 i9100_media_semaphore_record(3,0x81230000,0,1,0,1,0);assert(i9100_media_semaphore_diag[0]==2);
 now_task=&other;i9100_media_semaphore_record(4,0x81234567,UINT32_MAX,1,0,1,0);
 assert(row(3)[6]==(unsigned)(uintptr_t)&other&&row(3)[11]==0);
 now_task=&task;i9100_media_semaphore_record(5,0x81234567,0,0,0,0,0);
 assert(i9100_media_semaphore_diag[0]==4&&i9100_media_semaphore_diag[8]==5&&!i9100_media_semaphore_target);
 assert(userreads==copies&&pidreads==pids&&!i9100_media_semaphore_guard);
 i9100_media_semaphore_record(4,0x81234567,0,0,0,0,0);assert(i9100_media_semaphore_diag[0]==4);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_media_semaphore_diag,1,sizeof(i9100_media_semaphore_diag),f);fclose(f);
 f=fopen(argv[2],"wb");assert(f);fwrite((const void*)i9100_media_dispatch_wait,1,sizeof(i9100_media_dispatch_wait),f);fclose(f);
 for(unsigned n=0;n<10;n++){
  reset();switch(n){case 0:real_ncpus=2;break;case 1:irq=1;break;case 2:interrupts=0;break;case 3:preemption=1;break;
   case 4:pid=34;break;case 5:now_task=kernel_task;break;case 6:i9100_audio_property_lock_history[3]=0xdead;break;
   case 7:state.pc+=4;break;case 8:badframe=1;break;case 9:thread.machine.uss=NULL;break;}
  i9100_media_semaphore_begin(0x81234567,0xa203);assert(!i9100_media_semaphore_diag[0]&&!i9100_media_dispatch_wait[0]);
 }
 reset();i9100_media_semaphore_begin(0x81234567,0xa203);
 for(unsigned n=0;n<200;n++)i9100_media_semaphore_record(2,0x81234567,0,1,0,0,0);
 assert(i9100_media_semaphore_diag[0]==128&&i9100_media_semaphore_diag[9]>0&&row(1)[1]==1);
 i9100_media_semaphore_record(8,0x81234567,0,1,0,0,0);assert(i9100_media_semaphore_diag[8]==8&&!i9100_media_semaphore_target);
 puts("PASS production exact CMSession scope, pre-wait queue, cross-task same-object signal, no user/PID reads in masked hooks, completion/destroy lifetime and bounded loss; IPC/scheduling modeled");
}
'''
(out / 'check.c').write_text(source)
subprocess.run(['clang', '-O1', '-g', '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', str(out / 'check.c'), '-o', str(out / 'check')], check=True)
subprocess.run([str(out / 'check'), str(out / 'semaphore.bin'), str(out / 'queue-wait.bin')], check=True)
print('EVIDENCE', out)
