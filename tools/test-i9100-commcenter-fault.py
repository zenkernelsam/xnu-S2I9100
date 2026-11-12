#!/usr/bin/env python3
"""Run the production assertion witness with modeled copyin and task state."""
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                         sys.executable, '-B', __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
repo = Path(__file__).resolve().parents[1]
source = (repo / 'osfmk/arm/i9100_commcenter_fault.h').read_text()
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UInt32, user_addr_t;
struct proc {unsigned pid;const char *name;};struct task {struct proc *bsd_info;};
struct thread {struct task *task;};typedef struct thread *thread_t;
struct i9100_fault_snapshot {uint32_t psr,type,pc,lr,sp,far;};
static struct proc process={31,"CommCenterClassi"};static struct task task={&process};
static struct thread thread={&task};static unsigned irq,preemption,interrupts=1,enabled=1,copies,checks;
static unsigned failure=0xffffffff;static const unsigned char message[]="success";
static thread_t current_thread(void){return &thread;}
static int get_preemption_level(void){return preemption;}
static int ml_get_interrupts_enabled(void){return interrupts;}
static int ml_at_interrupt_context(void){return irq;}
static int PE_parse_boot_argn(const char *n,void *out,unsigned size){(void)n;(void)size;*(int*)out=enabled;return 1;}
static const char *proc_name_address(struct proc *p){return p->name;}
static unsigned proc_pid(struct proc *p){return p->pid;}
static int OSCompareAndSwap(unsigned before,unsigned after,volatile uint32_t *p){if(*p!=before)return 0;*p=after;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int copyin(user_addr_t address,void *out,unsigned size){
 ++copies;if(size!=1 || address<0x1d146a || address-0x1d146a>=sizeof(message) || address-0x1d146a>=failure)return 14;
 *(unsigned char*)out=message[address-0x1d146a];return 0;
}
static void check(int valid,const char *why){checks++;if(!valid){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
'''
suffix = r'''
static void reset(void){memset((void*)i9100_commcenter_fault_payload,0,sizeof(i9100_commcenter_fault_payload));i9100_commcenter_fault_guard=0;copies=0;}
int main(void){
 struct i9100_fault_snapshot state={0x30,4,0x1a58b4,0x3907d0e7,0x2fd83000,1};uint32_t regs[13]={1};regs[4]=0x1d146a;regs[7]=0x2fd83010;
 i9100_commcenter_fault_note(&state,regs);
 check(i9100_commcenter_fault_payload[0]==1 && i9100_commcenter_fault_payload[1]==31,"first owning process retained");
 check(i9100_commcenter_fault_payload[7]==0x10000 && i9100_commcenter_fault_payload[10]==8 && i9100_commcenter_fault_payload[12],"native slide and terminated expression");
 check(!memcmp((const void*)&i9100_commcenter_fault_payload[16],message,sizeof(message)),"original bytes unchanged");
 process.pid=78;i9100_commcenter_fault_note(&state,regs);check(i9100_commcenter_fault_payload[1]==31,"first fault cannot be overwritten");process.pid=31;
 reset();irq=1;i9100_commcenter_fault_note(&state,regs);check(!copies,"IRQ cannot fault in user bytes");irq=0;
 reset();preemption=1;i9100_commcenter_fault_note(&state,regs);check(!copies,"preemption gate");preemption=0;
 reset();interrupts=0;i9100_commcenter_fault_note(&state,regs);check(!copies,"masked interrupt gate");interrupts=1;
 reset();enabled=0;i9100_commcenter_fault_note(&state,regs);check(!copies,"explicit diagnostic switch");enabled=1;
 reset();process.name="Other";i9100_commcenter_fault_note(&state,regs);check(!copies,"only Classic process");process.name="CommCenterClassi";
 reset();state.pc+=2;i9100_commcenter_fault_note(&state,regs);check(!copies,"unrelated instruction rejected");state.pc-=2;
 reset();regs[0]=0;i9100_commcenter_fault_note(&state,regs);check(!copies,"exact store value");regs[0]=1;
 reset();state.far=2;i9100_commcenter_fault_note(&state,regs);check(!copies,"exact fault address");state.far=1;
 reset();failure=3;i9100_commcenter_fault_note(&state,regs);check(i9100_commcenter_fault_payload[10]==3 && i9100_commcenter_fault_payload[11]==14 && !i9100_commcenter_fault_payload[12],"partial bytes and actual copy error retained");failure=0xffffffff;
 reset();regs[4]=0xffffffff;i9100_commcenter_fault_note(&state,regs);check(!copies,"pointer range rejected");
 printf("%u production assertion witness checks passed; copyin and task state modeled\n",checks);
}
'''
out = repo / 'out' / ('commcenter-fault-check-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True)
c = out / 'witness.c'
binary = out / 'witness'
c.write_text(prefix + source + suffix)
subprocess.run(['clang', '-std=c11', '-O1', '-g', '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', str(c), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print(out, flush=True)
