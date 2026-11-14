#!/usr/bin/env python3
"""Exercise production first-media raw snapshot and unlocked own-task reads."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('media-threads-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
body=(repo/'osfmk/arm/i9100_media_threads_snapshot.h').read_text();body='\n'.join(l for l in body.splitlines() if not l.startswith('#include'))
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32,user_addr_t,vm_size_t;
typedef struct entry {struct entry*next,*prev;} *queue_entry_t,queue_head_t;
struct task;typedef struct task*task_t;
typedef struct {uint32_t r[13],sp,lr,pc,cpsr;} arm_saved_state_t;
struct uthread {uint32_t words[12];};
/* Mach queue_enter links point to the object, not its embedded chain.
 * Keep the chain away from offset zero, as in the real struct thread. */
struct thread {uint32_t prefix[17];queue_head_t task_threads;task_t task;uint64_t thread_id;unsigned state;struct {arm_saved_state_t*uss;} machine;struct uthread*uthread;};typedef struct thread*thread_t;
struct task {queue_head_t threads;};
static struct task task;static struct thread threads[20];static struct uthread uthreads[20];static arm_saved_state_t registers[20];
static task_t current_task(void){return &task;}static task_t kernel_task;
unsigned int real_ncpus=1;volatile uint32_t i9100_audio_property_runtime[4];volatile uint32_t i9100_audio_property_lock_history[97];
static unsigned masks,reads,userreads,irq,interrupts=1,preemption,enable=1,failread;
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,int*out,unsigned bytes){assert(!strcmp(n,"i9100xpcdiag")&&bytes==4);*out=enable;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned val,volatile unsigned*p){if(*p!=old)return 0;*p=val;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
#define I9100_SNAPSHOT_MASK_GET(out) do{out=masks;masks=0xc0;}while(0)
#define I9100_SNAPSHOT_MASK_SET(out) do{masks=out;}while(0)
#define queue_end(head,item) ((queue_entry_t)(head)==(item))
#define I9100_MEDIA_THREADS_CAP 16U
#define I9100_MEDIA_THREADS_STRIDE 72U
static int i9100_irq_snapshot_read(const void*in,void*out,unsigned n){assert(masks==0xc0&&n<=256);reads++;if(reads==failread)return 0;memcpy(out,in,n);return 1;}
static int copyin(user_addr_t at,void*out,unsigned n){
 assert(!masks);userreads++;
 if(at==0x20000&&n==64){uint32_t w[16]={1,2,0x12000,0x4d555458,0x12068,1,0,1};memcpy(out,w,n);return 0;}
 if(at>=0x12000&&at<0x13000&&n==48){uint32_t w[12]={0x4d555458,0,0x12068,0,0x303,0x100,0,0,578,0};memcpy(out,w,n);return 0;}
 if(at>=0x30020&&at<0x32000&&n==8){uint32_t pair[2]={0,0x310fdd65};memcpy(out,pair,n);return 0;}
 if(at==0x392cd0fc&&n==16){uint32_t code[4]={0xe1a00000,0xe12fff1e,0x11223344,0x55667788};memcpy(out,code,n);return 0;}
 return 14;
}
#define BOARD_CONFIG_EXYNOS4210_I9100 1
'''+body+r'''
static void reset(unsigned count){
 memset((void*)i9100_media_threads,0,sizeof(i9100_media_threads));i9100_media_threads_once=0;reads=userreads=failread=0;masks=irq=preemption=0;interrupts=enable=real_ncpus=1;
 task.threads.next=count?(queue_entry_t)&threads[0]:(queue_entry_t)&task.threads;
 for(unsigned n=0;n<count;n++){
  threads[n].task_threads.next=n+1<count?(queue_entry_t)&threads[n+1]:(queue_entry_t)&task.threads;
  threads[n].task=&task;threads[n].thread_id=578+n;threads[n].state=1;threads[n].machine.uss=&registers[n];threads[n].uthread=&uthreads[n];
  for(unsigned k=0;k<12;k++)uthreads[n].words[k]=0x5500+k;
  registers[n].r[0]=0x12000+n*64;registers[n].r[7]=0x30020+n*128;registers[n].r[12]=301;registers[n].sp=0x30000+n*128;registers[n].pc=0x392cd0fc;registers[n].cpsr=0x10;
 }
 i9100_audio_property_lock_history[0]=1;i9100_audio_property_lock_history[2]=33;i9100_audio_property_lock_history[3]=(uint32_t)(uintptr_t)&task;
 i9100_audio_property_runtime[1]=33;i9100_audio_property_runtime[2]=(uint32_t)(uintptr_t)&task;i9100_audio_property_runtime[3]=0x20000;
}
int main(int argc,char**argv){
 assert(argc==2);reset(3);i9100_media_threads_note(33,0x100000);
 assert(i9100_media_threads[0]==1&&i9100_media_threads[1]==2&&i9100_media_threads[4]==3&&!i9100_media_threads[5]);
 volatile uint32_t*r=&i9100_media_threads[16];assert(r[2]==578&&!r[5]&&r[23]==1&&r[28]==0x4d555458&&r[36]==578&&r[42]==0x310fdd65&&r[60]==0x5500&&r[66]==1);
 assert(!r[67]&&r[68]==0xe1a00000&&r[71]==0x55667788);
 assert(i9100_media_threads[11]==2&&i9100_media_threads[12]==1&&!masks);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_media_threads,1,sizeof(i9100_media_threads),f);fclose(f);
 unsigned seen=reads;i9100_media_threads_note(33,0x100000);assert(reads==seen);
 for(unsigned n=0;n<5;n++){reset(3);if(n==0)real_ncpus=2;if(n==1)irq=1;if(n==2)preemption=1;if(n==3)enable=0;if(n==4)i9100_audio_property_lock_history[2]=34;i9100_media_threads_note(33,0);assert(!reads&&!userreads&&!i9100_media_threads[0]);}
 reset(20);i9100_media_threads_note(33,0);assert(i9100_media_threads[4]==16&&i9100_media_threads[5]==3);
 reset(3);failread=1;i9100_media_threads_note(33,0);assert(i9100_media_threads[5]==1&&!i9100_media_threads[4]&&!masks);
 reset(3);registers[0].pc=0x39300000;i9100_media_threads_note(33,0);assert(i9100_media_threads[16+67]==14&&!i9100_media_threads[16+5]&&!masks);
 reset(3);registers[0].pc=0x3ffffff4;i9100_media_threads_note(33,0);assert(i9100_media_threads[16+67]==UINT32_MAX&&!masks);
 puts("PASS production first-media scope, UP/raw-read masks, unlock-before-copyin, first retention, handler words, cap/failure; objects and paging modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'media-first-threads.bin')],check=True);print('EVIDENCE',out)
