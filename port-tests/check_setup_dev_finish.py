#!/usr/bin/env python3
"""Exercise the production native-finish patch and its rollback/identity gates."""
import os,sys,subprocess
from pathlib import Path
from datetime import datetime
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('setup-dev-finish-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef int kern_return_t;typedef int*vm_map_t;struct task{unsigned acts;};typedef struct task*task_t;
struct proc{const char*p_comm;task_t task;unsigned p_pid,p_idversion;uint8_t p_uuid[16];};typedef struct proc*proc_t;
static int map;static struct task t={1};static struct proc p;
static unsigned enabled=1,abi=6,aks=1,fail,again,step,ops,irq,preempt;static uint8_t bytes[8];
#define KERN_SUCCESS 0
#define FALSE 0
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_COPY 16
#define EIO 5
#define OSMemoryBarrier() __sync_synchronize()
static int PE_parse_boot_argn(const char*k,int*v,unsigned n){assert(n==4);*v=!strcmp(k,"i9100setupdevfinish")?enabled:!strcmp(k,"i9100hidabi")?abi:aks;return 1;}
static task_t current_task(void){return &t;}static vm_map_t current_map(void){return &map;}
static vm_map_t get_task_map(task_t x){return x==&t?&map:0;}static unsigned get_task_numacts(task_t x){return x->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return 1;}
static unsigned get_preemption_level(void){return preempt;}static void i9100_ui_density_sync_current(void){}
static int copyin(unsigned at,void*dst,unsigned n);
static int bad(void){++step;return step==fail || step==again;}
static kern_return_t vm_map_protect(vm_map_t m,unsigned a,unsigned b,unsigned f,int maximum){assert(m==&map && a==0x13000 && b==0x14000 && !maximum && (f==5 || f==19));++ops;return bad()?4:0;}
static int copyout(const void*src,unsigned at,unsigned n){assert(at==0x1376c && n==8);int e=bad();memcpy(bytes,src,e?4:8);return e?14:0;}
static void flush_dcache(unsigned a,unsigned n,int physical){assert(a==0x1376c && n==8 && !physical);}
static void invalidate_icache(unsigned a,unsigned n,int physical){flush_dcache(a,n,physical);}
#include "i9100_setup_dev_finish.h"
static int copyin(unsigned at,void*dst,unsigned n){assert(n==8);if(at==0x1376c)memcpy(dst,bytes,8);else{assert(at==0x12f84);memcpy(dst,i9100_setup_finish_original,8);}return 0;}
static void reset(void){memset((void*)i9100_setup_dev_finish_diag,0,sizeof(i9100_setup_dev_finish_diag));memset(i9100_setup_dev_finish_pending,0,sizeof(i9100_setup_dev_finish_pending));memcpy(bytes,i9100_setup_run_original,8);p.p_comm="Setup";p.task=&t;p.p_pid=7;p.p_idversion=9;memcpy(p.p_uuid,i9100_setup_dev_finish_uuid,16);enabled=aks=1;abi=6;fail=again=step=ops=irq=preempt=0;t.acts=1;}
int main(void){reset();i9100_setup_dev_finish_record(&p,0x10000);assert(!i9100_setup_dev_finish_bootstrap(&p));uint32_t b[2];memcpy(b,bytes,8);assert(b[0]==0xf000f8df && b[1]==0x12f85 && i9100_setup_dev_finish_diag[1]==2);
for(unsigned n=1;n<=3;n++){reset();fail=n;i9100_setup_dev_finish_record(&p,0x10000);assert(!i9100_setup_dev_finish_bootstrap(&p));assert(!memcmp(bytes,i9100_setup_run_original,8));}
reset();fail=2;again=4;i9100_setup_dev_finish_record(&p,0x10000);assert(i9100_setup_dev_finish_bootstrap(&p)==EIO);
for(unsigned n=0;n<8;n++){reset();if(n==0)enabled=0;if(n==1)abi=7;if(n==2)aks=0;if(n==3)p.p_uuid[0]^=1;if(n==4)p.p_comm="Other";i9100_setup_dev_finish_record(&p,0x10000);if(n==5)t.acts=2;if(n==6)irq=1;if(n==7)preempt=1;assert(!i9100_setup_dev_finish_bootstrap(&p) && !ops);}
puts("PASS original run-to-finish target, partial writes restored, unsafe restore stops, UUID/AKS/ABI/context gating");}
'''
(out/'check.c').write_text(code);header=repo/'bsd/dev/arm'
for args in (['clang','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(header),str(out/'check.c'),'-o',str(out/'check')],[str(out/'check')]):
    result=subprocess.run(args,text=True,capture_output=True);(out/('compile.log' if args[0]=='clang' else 'result.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
print('EVIDENCE',out)
