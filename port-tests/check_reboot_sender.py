#!/usr/bin/env python3
"""Validate the production reboot sender observer and unchanged message."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('reboot-sender-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
body='\n'.join(x for x in (repo/'osfmk/ipc/i9100_reboot_sender.h').read_text().splitlines() if not x.startswith('#include'))
source=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UInt32;typedef uintptr_t vm_offset_t;typedef void*thread_t;
typedef struct {uint32_t r[13],sp,lr,pc,cpsr;}arm_saved_state_t;
typedef struct {char*p_comm;unsigned p_pid;}proc,*proc_t;
typedef struct {unsigned msgh_bits,msgh_size,remote,local,voucher;int msgh_id;}mach_msg_header_t;
typedef struct {mach_msg_header_t*ikm_header;unsigned ikm_size;}ipc_kmsg,*ipc_kmsg_t;
static proc current={"backboardd",8};static arm_saved_state_t state;static unsigned enabled=1,irq,interrupts=1,preempt,reads,readerror;
static unsigned proc_selfpid(void){return current.p_pid;}static void proc_name(unsigned pid,char*out,unsigned size){assert(pid==current.p_pid);snprintf(out,size,"%s",current.p_comm);}static void*current_task(void){return(void*)0xc2000000;}
static thread_t current_thread(void){return(void*)1;}static void*find_user_regs(thread_t t){assert(t==(void*)1);return &state;}
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return interrupts;}static unsigned get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char*n,int*v,unsigned z){assert(!strcmp(n,"i9100xpcdiag")&&z==4);*v=enabled;return 1;}
static int OSCompareAndSwap(unsigned a,unsigned b,volatile unsigned*p){if(*p!=a)return 0;*p=b;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
#define MACH_MSGH_BITS_COMPLEX 0x80000000
static int copyin(unsigned a,void*out,unsigned n){assert(n==8&&a>=0x2000&&a<=0x12000&&!irq&&interrupts&&!preempt);reads++;if(readerror)return 14;uint32_t v[]={a+16,0x31000101+reads*4};memcpy(out,v,8);return 0;}
''' + body + r'''
static void reset(ipc_kmsg_t k){
 memset((void*)i9100_reboot_sender_diag,0,sizeof(i9100_reboot_sender_diag));i9100_reboot_sender_diag[0]=1;
 current.p_comm="backboardd";enabled=interrupts=1;irq=preempt=reads=readerror=0;state.sp=0x2000;state.r[7]=0x2010;state.cpsr=0x10;state.pc=0x31000000;state.lr=0x31000101;
 k->ikm_size=40;k->ikm_header=(void*)(k+1);memset(k+1,0,40);k->ikm_header->msgh_size=40;k->ikm_header->msgh_id=401;((uint8_t*)(k+1))[28]=1;
}
int main(void){
 struct {ipc_kmsg k;uint8_t body[40];}packet;uint8_t before[40];ipc_kmsg_t k=&packet.k;
 reset(k);memcpy(before,packet.body,40);i9100_reboot_sender_note(k);assert(reads==8&&i9100_reboot_sender_diag[1]==1&&i9100_reboot_sender_diag[4]==1&&i9100_reboot_sender_diag[14]==8&&!memcmp(before,packet.body,40));
 for(unsigned n=0;n<8;n++)i9100_reboot_sender_note(k);assert(i9100_reboot_sender_diag[1]==4&&reads==32);
 for(unsigned n=0;n<10;n++){reset(k);if(n==0)enabled=0;if(n==1)irq=1;if(n==2)interrupts=0;if(n==3)preempt=1;if(n==4)current.p_comm="SpringBoard";if(n==5)state.cpsr=0x13;if(n==6)k->ikm_header->msgh_id=402;if(n==7)k->ikm_header->msgh_size=39;if(n==8)k->ikm_header->msgh_bits=0x80000000;if(n==9)k->ikm_size=39;i9100_reboot_sender_note(k);assert(!reads&&!i9100_reboot_sender_diag[1]);}
 reset(k);readerror=1;i9100_reboot_sender_note(k);assert(reads==1&&i9100_reboot_sender_diag[14]==1&&i9100_reboot_sender_diag[19]==14);
 puts("PASS current-user context, request identity, bounded frames/capacity/read-error and untouched reboot request");
}
'''
(out/'check.c').write_text(source)
r=subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined',out/'check.c','-o',out/'check'],capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([out/'check'],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.splitlines()[-1]);print('EVIDENCE',out)
