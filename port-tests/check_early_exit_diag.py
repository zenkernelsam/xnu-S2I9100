#!/usr/bin/env python3
"""Exercise production exit scalars and remote-context exclusion; lifetime modeled."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('early-exit-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
src=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32;typedef int32_t SInt32;
typedef struct{uint32_t r[13],sp,lr,pc,cpsr;} arm_saved_state_t;
struct uthread{unsigned uu_exception,uu_code,uu_subcode;};
struct proc{int p_pid,p_idversion;void*task;unsigned p_csflags;};typedef struct proc*proc_t;
struct thread{void*task;};typedef struct thread*thread_t;
static unsigned enabled=1,reads;static arm_saved_state_t saved;static void*state=&saved;
static void*get_threadtask(thread_t t){return t->task;}
void*find_user_regs(thread_t t){assert(t);reads++;return state;}
static int PE_parse_boot_argn(const char*n,int*out,unsigned size){assert(!strcmp(n,"i9100xpcdiag")&&size==4);*out=enabled;return 1;}
static int OSIncrementAtomic(volatile SInt32*p){return ++*p;}
#define OSMemoryBarrier() __sync_synchronize()
''' + f'#include "{repo}/bsd/kern/i9100_early_exit_diag.h"\n' + r'''
int main(int argc,char**argv){
 assert(argc==2);struct proc p={14,14,(void*)0xc2000000,0x123};struct thread th={(void*)0xc2000000};
 struct uthread ut={1,2,3};saved.pc=0x392cc001;saved.lr=0x18ebf;saved.cpsr=0x10;
 i9100_early_exit_note(&p,0,&th,&ut);volatile uint32_t*r=&i9100_early_exit_diag[4+14*16];
 assert(r[0]==1&&r[4]==0&&r[5]==1&&r[6]==1&&r[7]==saved.pc&&r[10]==1);
 unsigned old=reads;th.task=(void*)0xc3000000;i9100_early_exit_note(&p,9,&th,&ut);
 assert(reads==old&&r[4]==9&&!r[5]&&!r[6]&&!r[7]&&!r[8]&&!r[10]);
 th.task=p.task;state=NULL;i9100_early_exit_note(&p,0x100,&th,NULL);assert(r[4]==0x100&&r[5]&&!r[6]&&!r[10]);
 for(unsigned n=0;n<3;n++){p.p_pid=n==0?0:n==1?64:15;enabled=n==2?0:1;i9100_early_exit_note(&p,0,&th,&ut);assert(!i9100_early_exit_diag[4+15*16]);}
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_early_exit_diag,1,sizeof(i9100_early_exit_diag),f);fclose(f);
 puts("PASS production original 0/SIG9/exit1 statuses, owned registers, null saved state, remote killing-context exclusion and bounds; lifetime modeled");
}
'''
(out/'check.c').write_text(src)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'early-exit.bin')],check=True);print('EVIDENCE',out)
