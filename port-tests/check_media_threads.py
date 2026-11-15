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
struct thread {uint32_t prefix[17];queue_head_t task_threads;task_t task;uint64_t thread_id;unsigned state;struct {arm_saved_state_t*uss;uint64_t cthread_self;} machine;struct uthread*uthread;};typedef struct thread*thread_t;
struct task {queue_head_t threads;};
static struct task task;static struct thread threads[20];static struct uthread uthreads[20];static arm_saved_state_t registers[20];
static task_t current_task(void){return &task;}static task_t kernel_task;
unsigned int real_ncpus=1;volatile uint32_t i9100_audio_property_runtime[4];volatile uint32_t i9100_audio_property_lock_history[97];
static unsigned masks,reads,userreads,irq,interrupts=1,preemption,enable=1,failread,current_pid=33,repair,target_allowed=1;
static const void *fail_source;
static unsigned fail_user;
int proc_selfpid(void){return (int)current_pid;}
volatile uint32_t i9100_audio_observation_target[8];
int i9100_audio_select_task(unsigned pid,unsigned owner){return target_allowed&&i9100_audio_observation_target[1]==2&&pid==i9100_audio_observation_target[2]&&owner==i9100_audio_observation_target[3];}
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,int*out,unsigned bytes){assert(bytes==4);if(!strcmp(n,"i9100xpcdiag"))*out=enable;else {assert(!strcmp(n,"i9100audiolock"));*out=repair;}return 1;}
static int OSCompareAndSwap(unsigned old,unsigned val,volatile unsigned*p){if(*p!=old)return 0;*p=val;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
#define I9100_SNAPSHOT_MASK_GET(out) do{out=masks;masks=0xc0;}while(0)
#define I9100_SNAPSHOT_MASK_SET(out) do{masks=out;}while(0)
#define queue_end(head,item) ((queue_entry_t)(head)==(item))
#define I9100_MEDIA_THREADS_CAP 16U
#define I9100_MEDIA_THREADS_STRIDE 72U
#define I9100_MEDIA_IDENTITY_STRIDE 8U
#define I9100_AUDIO_OWNER_CAP 8U
#define I9100_AUDIO_OWNER_STRIDE 64U
static int i9100_irq_snapshot_read(const void*in,void*out,unsigned n){assert(masks==0xc0&&n<=256);reads++;if(reads==failread || in==fail_source)return 0;memcpy(out,in,n);return 1;}
static int copyin(user_addr_t at,void*out,unsigned n){
 assert(!masks);userreads++;
 if(at==fail_user)return 14;
 if(at>=0x40000&&at<0x42000&&n==4){uint32_t self=at+0x80;memcpy(out,&self,n);return 0;}
 if(at>=0x11ffc&&at<0x13000&&n==4){uint32_t owner=0x40080;memcpy(out,&owner,n);return 0;}
 if(at==0x20000&&n==64){uint32_t w[16]={1,2,0x12000,0x4d555458,0x12068,1,0,1};memcpy(out,w,n);return 0;}
 if(at>=0x12000&&at<0x13000&&n==48){uint32_t w[12]={0x4d555458,0,0x12068,0,0x303,0x100,0,0,578,0};memcpy(out,w,n);return 0;}
 if(at>=0x30020&&at<0x32000&&n==8){uint32_t pair[2]={0,0x310fdd65};memcpy(out,pair,n);return 0;}
 if(at==0x392cd0fc&&n==16){uint32_t code[4]={0xe1a00000,0xe12fff1e,0x11223344,0x55667788};memcpy(out,code,n);return 0;}
 if(at==0x39594dd0&&n==4){uint32_t q=0x25000;memcpy(out,&q,n);return 0;}
 if(at==0x25000&&n==256){memset(out,0,n);strcpy((char*)out+64,"com.apple.coremedia.fixture");return 0;}
 return 14;
}
#define BOARD_CONFIG_EXYNOS4210_I9100 1
'''+body+r'''
static void reset(unsigned count){
 memset((void*)i9100_media_threads,0,sizeof(i9100_media_threads));i9100_media_threads_once=0;reads=userreads=failread=0;masks=irq=preemption=0;interrupts=enable=real_ncpus=1;current_pid=33;
 memset((void*)i9100_media_identity,0,sizeof(i9100_media_identity));fail_source=NULL;fail_user=0;
 memset((void*)i9100_media_dispatch_diag,0,sizeof(i9100_media_dispatch_diag));
 repair=0;target_allowed=1;i9100_audio_observation_target[0]=1;i9100_audio_observation_target[1]=2;i9100_audio_observation_target[2]=33;i9100_audio_observation_target[3]=(unsigned)(uintptr_t)&task;i9100_audio_observation_target[4]=0x100000;i9100_audio_observation_target[5]=0x20000;
 task.threads.next=count?(queue_entry_t)&threads[0]:(queue_entry_t)&task.threads;
 for(unsigned n=0;n<count;n++){
  threads[n].task_threads.next=n+1<count?(queue_entry_t)&threads[n+1]:(queue_entry_t)&task.threads;
  threads[n].task=&task;threads[n].thread_id=578+n;threads[n].state=1;threads[n].machine.uss=&registers[n];threads[n].uthread=&uthreads[n];
  threads[n].machine.cthread_self=0x40003+n*128;
  for(unsigned k=0;k<12;k++)uthreads[n].words[k]=0x5500+k;
  registers[n].r[0]=0x12000+n*64;registers[n].r[7]=0x30020+n*128;registers[n].r[12]=301;registers[n].sp=0x30000+n*128;registers[n].pc=0x392cd0fc;registers[n].cpsr=0x10;
 }
 i9100_audio_property_lock_history[0]=1;i9100_audio_property_lock_history[2]=33;i9100_audio_property_lock_history[3]=(uint32_t)(uintptr_t)&task;
 i9100_audio_property_runtime[1]=33;i9100_audio_property_runtime[2]=(uint32_t)(uintptr_t)&task;i9100_audio_property_runtime[3]=0x20000;
}
int main(int argc,char**argv){
 assert(argc==7);reset(3);i9100_media_threads_note(33,0x100000);
 assert(i9100_media_threads[0]==1&&i9100_media_threads[1]==2&&i9100_media_threads[4]==3&&!i9100_media_threads[5]);
 volatile uint32_t*r=&i9100_media_threads[16];assert(r[2]==578&&!r[5]&&r[23]==1&&r[28]==0x4d555458&&r[36]==578&&r[42]==0x310fdd65&&r[60]==0x5500&&r[66]==1);
 assert(!r[67]&&r[68]==0xe1a00000&&r[71]==0x55667788);
 assert(i9100_media_threads[11]==2&&i9100_media_threads[12]==1&&!masks);
 assert(i9100_media_dispatch_diag[0]==1&&i9100_media_dispatch_diag[5]==0x39594dd0&&i9100_media_dispatch_diag[7]==0x25000&&!i9100_media_dispatch_diag[8]&&i9100_media_dispatch_diag[9]==256);
 assert(!strcmp((const char*)&i9100_media_dispatch_diag[16+16],"com.apple.coremedia.fixture"));
 assert(i9100_media_identity[0]==1&&i9100_media_identity[4]==3&&i9100_media_identity[5]==16&&i9100_media_identity[6]==8);
 volatile uint32_t*id=&i9100_media_identity[8];assert(id[0]==1&&id[1]==0x40003&&!id[2]&&!id[3]&&id[4]==0x40080&&!id[5]&&id[6]==0x40080&&!id[7]);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_media_threads,1,sizeof(i9100_media_threads),f);fclose(f);
 f=fopen(argv[3],"wb");assert(f);fwrite((const void*)i9100_media_dispatch_diag,1,sizeof(i9100_media_dispatch_diag),f);fclose(f);
 f=fopen(argv[6],"wb");assert(f);fwrite((const void*)i9100_media_identity,1,sizeof(i9100_media_identity),f);fclose(f);
 i9100_audio_owner_note(1,33,301,0x12000,579,0);
 volatile uint32_t*owner=&i9100_audio_owner_diag[4];
 assert(i9100_audio_owner_diag[0]==1&&owner[0]==1&&owner[5]==579&&!owner[9]&&owner[16]==0x12040&&owner[31]==0x392cd0fc&&owner[10]==1&&owner[48]==0x310fdd65&&!masks);
 i9100_audio_owner_note(2,33,301,0x12000,999,0);assert(i9100_audio_owner_diag[0]==2&&i9100_audio_owner_diag[4+64+9]==2&&!masks);
 i9100_audio_owner_note(3,33,302,0x12000,UINT32_MAX,UINT32_MAX);assert(i9100_audio_owner_diag[0]==2);
 f=fopen(argv[2],"wb");assert(f);fwrite((const void*)i9100_audio_owner_diag,1,sizeof(i9100_audio_owner_diag),f);fclose(f);
 unsigned seen=reads;i9100_media_threads_note(33,0x100000);assert(reads==seen);
 /* An untouched native task has no repair history or mutable callback page. */
 reset(3);memset((void*)i9100_audio_property_lock_history,0,sizeof(i9100_audio_property_lock_history));
 memset((void*)i9100_audio_property_runtime,0,sizeof(i9100_audio_property_runtime));
 i9100_media_threads_note(33,0x100000);
 assert(i9100_media_threads[0]==1&&i9100_media_threads[4]==3&&i9100_media_threads[9]==UINT32_MAX);
 assert(!i9100_media_threads[8]&&!i9100_media_threads[10]&&!i9100_media_threads[11]&&!i9100_media_threads[12]&&!masks);
 assert(i9100_media_threads[16+2]==578&&i9100_media_dispatch_diag[9]==256);
 f=fopen(argv[4],"wb");assert(f);fwrite((const void*)i9100_media_threads,1,sizeof(i9100_media_threads),f);fclose(f);
 reset(3);repair=1;target_allowed=0;i9100_media_threads_note(33,0x100000);
 assert(!i9100_media_threads[0]&&!i9100_media_threads_once);
 current_pid=34;target_allowed=1;i9100_audio_observation_target[2]=34;i9100_audio_property_runtime[1]=34;
 i9100_media_threads_note(34,0x100000);assert(i9100_media_threads[0]==1&&i9100_media_threads[2]==34&&i9100_media_threads[13]==1);
 f=fopen(argv[5],"wb");assert(f);fwrite((const void*)i9100_media_threads,1,sizeof(i9100_media_threads),f);fclose(f);
 for(unsigned n=0;n<8;n++){reset(3);unsigned slide=0;if(n==0)real_ncpus=2;if(n==1)irq=1;if(n==2)preemption=1;if(n==3)enable=0;if(n==4)current_pid=34;if(n==5)slide=1;if(n==6)interrupts=0;if(n==7)slide=0x40000000U-0x392cc840U;i9100_media_threads_note(33,slide);assert(!reads&&!userreads&&!i9100_media_threads[0]);}
 reset(20);i9100_media_threads_note(33,0);assert(i9100_media_threads[4]==16&&i9100_media_threads[5]==3);
 reset(3);failread=1;i9100_media_threads_note(33,0);assert(i9100_media_threads[5]==1&&!i9100_media_threads[4]&&!masks);
 reset(3);registers[0].pc=0x39300000;i9100_media_threads_note(33,0);assert(i9100_media_threads[16+67]==14&&!i9100_media_threads[16+5]&&!masks);
 reset(3);registers[0].pc=0x3ffffff4;i9100_media_threads_note(33,0);assert(i9100_media_threads[16+67]==UINT32_MAX&&!masks);
 reset(3);threads[0].machine.cthread_self=0x100040003ULL;i9100_media_threads_note(33,0);assert(id[1]==0x40003&&id[2]==1&&!id[3]&&id[5]==UINT32_MAX);
 reset(3);threads[0].machine.cthread_self=3;i9100_media_threads_note(33,0);assert(!id[3]&&id[5]==UINT32_MAX);
 reset(3);fail_source=&threads[0].machine.cthread_self;i9100_media_threads_note(33,0);assert(id[3]==UINT32_MAX&&id[5]==UINT32_MAX&&!i9100_media_threads[16+5]);
 reset(3);fail_user=0x40000;i9100_media_threads_note(33,0);assert(!id[3]&&id[5]==14&&!id[4]);
 reset(3);fail_user=0x11ffc;i9100_media_threads_note(33,0);assert(id[7]==14&&!id[6]);
 reset(3);registers[0].r[12]=31;i9100_media_threads_note(33,0);assert(!id[5]&&id[7]==UINT32_MAX);
 reset(3);registers[0].r[0]=4096;i9100_media_threads_note(33,0);assert(id[7]==UINT32_MAX);
 reset(3);registers[0].r[0]=0x12001;i9100_media_threads_note(33,0);assert(id[7]==UINT32_MAX);
 puts("PASS production first-media with repair ON/OFF, current PID, bounded slide, UP/raw masks, unlock-before-copyin, first retention, handler words, cap/failure; objects and paging modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'media-first-threads.bin'),str(out/'audio-owner.bin'),str(out/'media-dispatch.bin'),str(out/'media-first-native.bin'),str(out/'media-selected.bin'),str(out/'media-identity.bin')],check=True);print('EVIDENCE',out)
