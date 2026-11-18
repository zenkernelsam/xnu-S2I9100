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
static unsigned enabled=1,abi=6,aks=1,fail,again,step,ops,irq,preempt,removed,readfail,reads;static uint8_t bytes[8],codebytes[132];
#define KERN_SUCCESS 0
#define FALSE 0
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_COPY 16
#define VM_FLAGS_ANYWHERE 1
#define VM_OBJECT_NULL 0
#define VM_PROT_ALL 7
#define VM_INHERIT_COPY 1
typedef unsigned vm_map_offset_t;
#define EIO 5
#define OSMemoryBarrier() __sync_synchronize()
static int PE_parse_boot_argn(const char*k,int*v,unsigned n){assert(n==4);*v=!strcmp(k,"i9100setupdevfinish")?enabled:!strcmp(k,"i9100hidabi")?abi:aks;return 1;}
static task_t current_task(void){return &t;}static vm_map_t current_map(void){return &map;}
static vm_map_t get_task_map(task_t x){return x==&t?&map:0;}static unsigned get_task_numacts(task_t x){return x->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return 1;}
static unsigned get_preemption_level(void){return preempt;}static void i9100_ui_density_sync_current(void){}
static int copyin(unsigned at,void*dst,unsigned n);
static int bad(void){++step;return step==fail || step==again;}
static kern_return_t vm_map_enter(vm_map_t m,unsigned*a,unsigned n,unsigned mask,unsigned flags,void*obj,unsigned off,int copy,unsigned prot,unsigned max,unsigned inherit){assert(m==&map && n==4096 && !mask && flags==1 && !obj && !off && !copy && prot==3 && max==7 && inherit==1);*a=0x40000;++ops;return bad()?4:0;}
static kern_return_t vm_map_protect(vm_map_t m,unsigned a,unsigned b,unsigned f,int maximum){assert(m==&map && (a==0x12000 || a==0x40000) && b==a+4096 && !maximum && (f==5 || f==19));++ops;return bad()?4:0;}
static void vm_map_remove(vm_map_t m,unsigned a,unsigned b,unsigned f){assert(m==&map && a==0x40000 && b==a+4096 && !f);removed++;}
static int copyout(const void*src,unsigned at,unsigned n){assert((at==0x1276c && n==8) || (at==0x40000 && n==132));int e=bad();memcpy(at==0x1276c?bytes:codebytes,src,e?n/2:n);return e?14:0;}
static void flush_dcache(unsigned a,unsigned n,int physical){assert(((a==0x1276c && n==8) || (a==0x40000 && n==132)) && !physical);}
static void invalidate_icache(unsigned a,unsigned n,int physical){flush_dcache(a,n,physical);}
#include "i9100_setup_dev_finish.h"
static int copyin(unsigned at,void*dst,unsigned n){if(++reads==readfail)return 14;if(at==0x1276c){assert(n==8);memcpy(dst,bytes,8);}else if(at==0x11f84){assert(n==8);memcpy(dst,i9100_setup_finish_original,8);}else if(at==0x63f10){assert(n==4);memcpy(dst,"\x70\xf2\x9f\xe5",4);}else if(at==0x58f4e){assert(n==sizeof("performSelector:withObject:afterDelay:"));memcpy(dst,"performSelector:withObject:afterDelay:",n);}else{assert(at==0x52e52 && n==sizeof("_finishBuddy"));memcpy(dst,"_finishBuddy",n);}return 0;}
static void reset(void){memset((void*)i9100_setup_dev_finish_diag,0,sizeof(i9100_setup_dev_finish_diag));memset(i9100_setup_dev_finish_pending,0,sizeof(i9100_setup_dev_finish_pending));memcpy(bytes,i9100_setup_run_original,8);p.p_comm="Setup";p.task=&t;p.p_pid=7;p.p_idversion=9;memcpy(p.p_uuid,i9100_setup_dev_finish_uuid,16);enabled=aks=1;abi=6;fail=again=step=ops=irq=preempt=removed=readfail=reads=0;t.acts=1;}
int main(void){reset();i9100_setup_dev_finish_record(&p,0x10000);assert(!i9100_setup_dev_finish_bootstrap(&p));uint32_t b[2];memcpy(b,bytes,8);assert(b[0]==0xf000f8df && b[1]==0x40001 && i9100_setup_dev_finish_diag[1]==2 && !removed);FILE*f=fopen("code.bin","wb");assert(f && fwrite(codebytes,1,sizeof(codebytes),f)==sizeof(codebytes));fclose(f);
for(unsigned n=1;n<=6;n++){reset();fail=n;i9100_setup_dev_finish_record(&p,0x10000);assert(!i9100_setup_dev_finish_bootstrap(&p));assert(!memcmp(bytes,i9100_setup_run_original,8));assert(removed==(n!=1));}
reset();fail=5;again=8;i9100_setup_dev_finish_record(&p,0x10000);assert(i9100_setup_dev_finish_bootstrap(&p)==EIO && !removed);
for(unsigned n=1;n<=5;n++){reset();readfail=n;i9100_setup_dev_finish_record(&p,0x10000);assert(!i9100_setup_dev_finish_bootstrap(&p) && !ops);}
for(unsigned n=0;n<8;n++){reset();if(n==0)enabled=0;if(n==1)abi=7;if(n==2)aks=0;if(n==3)p.p_uuid[0]^=1;if(n==4)p.p_comm="Other";i9100_setup_dev_finish_record(&p,0x10000);if(n==5)t.acts=2;if(n==6)irq=1;if(n==7)preempt=1;assert(!i9100_setup_dev_finish_bootstrap(&p) && !ops);}
puts("PASS original run retained, delayed native-finish template, partial writes restored, unsafe restore pinned, UUID/AKS/ABI/context and original-byte gates");}
'''
(out/'check.c').write_text(code);header=repo/'bsd/dev/arm'
for args in (['clang','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(header),str(out/'check.c'),'-o',str(out/'check')],[str(out/'check')]):
    result=subprocess.run(args,text=True,capture_output=True,cwd=out);(out/('compile.log' if args[0]=='clang' else 'result.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
print('EVIDENCE',out)
