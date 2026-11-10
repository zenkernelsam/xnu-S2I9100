#!/usr/bin/env python3
"""Check the actual failed-parser snapshot gates/ranges with copyin modeled."""
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1];src=(repo/'osfmk/arm/trap.c').read_text()
start=src.index('static void i9100_ui_fault_stack_note(')
body=src[start:src.index('/* Ring of failed user exceptions',start)]
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UInt32,user_addr_t;
struct proc {int pid;const char *name;};struct task {struct proc *bsd_info;};
struct thread {struct task *task;};typedef struct thread *thread_t;
struct i9100_fault_snapshot {uint32_t psr,type,pc,lr,sp;};
static volatile uint32_t i9100_springboard_fault_stack[52],i9100_keyboard_fault_stack[52],i9100_ls_fault_stack[52],i9100_ls_fault_payload[2064],i9100_ui_fault_stack_guard;
static struct proc process={66,"lsd"};static struct task task={&process};static struct thread thread={&task};
static unsigned preemption,irq,interrupts=1,enabled=1,failPayload,checks,copyCalls;
static uint32_t frame[4]={0,0x12345,0x3000,128};static unsigned char payload[9000];
static thread_t current_thread(void){return &thread;}static int get_preemption_level(void){return preemption;}
static int ml_get_interrupts_enabled(void){return interrupts;}static int ml_at_interrupt_context(void){return irq;}
static int PE_parse_boot_argn(const char *n,void *out,unsigned size){(void)n;(void)size;*(int*)out=enabled;return 1;}
static const char *proc_name_address(struct proc *p){return p->name;}static int proc_pid(struct proc *p){return p->pid;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile uint32_t *p){if(*p!=old)return 0;*p=value;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int copyin(user_addr_t a,void *out,unsigned n){
 ++copyCalls;
 if(a>=0x2000 && a+n>=a && a+n<=0x2000+sizeof(frame)){memcpy(out,(unsigned char*)frame+(a-0x2000),n);return 0;}
 if(!failPayload && a>=0x3000 && a+n>=a && a+n<=0x3000+sizeof(payload)){memcpy(out,payload+(a-0x3000),n);return 0;}
 return 1;
}
static void check(int ok,const char *why){++checks;if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
static void reset(void){memset((void*)i9100_ls_fault_payload,0,sizeof(i9100_ls_fault_payload));memset((void*)i9100_ls_fault_stack,0,sizeof(i9100_ls_fault_stack));copyCalls=0;}
'''
suffix=r'''
int main(void){
 struct i9100_fault_snapshot state={0x30,4,0x312001d4,0x32564ab1,0x1ff0};uint32_t registers[13]={0};registers[7]=0x2000;
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(unsigned char)i;
 i9100_ui_fault_stack_note(&state,registers);
 check(i9100_ls_fault_payload[0]==1 && i9100_ls_fault_payload[1]==66 && i9100_ls_fault_payload[7]==0x3000,"actual pinned call and OOL arguments retained");
 check(i9100_ls_fault_payload[9]==128 && !i9100_ls_fault_payload[10] && !memcmp((const void*)&i9100_ls_fault_payload[16],payload,128),"input bytes preserved");
 reset();irq=1;i9100_ui_fault_stack_note(&state,registers);check(!copyCalls,"IRQ producer cannot copy user bytes");irq=0;
 reset();preemption=1;i9100_ui_fault_stack_note(&state,registers);check(!copyCalls,"preemption guard");preemption=0;
 reset();interrupts=0;i9100_ui_fault_stack_note(&state,registers);check(!copyCalls,"interrupt mask guard");interrupts=1;
 reset();enabled=0;i9100_ui_fault_stack_note(&state,registers);check(!copyCalls,"explicit diagnostics gate");enabled=1;
 reset();state.lr+=2;i9100_ui_fault_stack_note(&state,registers);check(!i9100_ls_fault_payload[0],"unrelated CF caller not reinterpreted");state.lr-=2;
 reset();registers[0]=1;i9100_ui_fault_stack_note(&state,registers);check(!i9100_ls_fault_payload[0],"only NULL type query");registers[0]=0;
 reset();process.name="Other";i9100_ui_fault_stack_note(&state,registers);check(!copyCalls,"process scope");process.name="lsd";
 reset();frame[2]=0x3ffffff0;frame[3]=128;i9100_ui_fault_stack_note(&state,registers);check(i9100_ls_fault_payload[10]==2 && !i9100_ls_fault_payload[9],"range cannot cross user ceiling");frame[2]=0x3000;
 reset();frame[3]=9000;i9100_ui_fault_stack_note(&state,registers);check(i9100_ls_fault_payload[9]==8192 && i9100_ls_fault_payload[11],"large payload clipped and marked");frame[3]=128;
 reset();failPayload=1;i9100_ui_fault_stack_note(&state,registers);check(i9100_ls_fault_payload[10]==3 && !i9100_ls_fault_payload[9],"copy fault preserved without fabricated data");failPayload=0;
 reset();registers[7]=0x1ff0;i9100_ui_fault_stack_note(&state,registers);check(i9100_ls_fault_payload[10]==1,"unavailable frame arguments retained");
 printf("%u production failed-parser guard/range checks passed; copyin modeled\n",checks);
}
'''
out=repo/'out'/('ls-fault-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir(parents=True)
c=out/'fault.c';binary=out/'fault';c.write_text(prefix+body+suffix)
subprocess.run(['clang','-std=c11','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True);print(out,flush=True)
