#!/usr/bin/env python3
"""Check production bounded identity retention; proc lifetime is modeled."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('early-process-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32;
struct proc {int p_pid,p_idversion;void*task;char p_comm[17];};typedef struct proc*proc_t;
static unsigned irq,interrupts=1,preemption,enabled=1,parses;
static int ml_at_interrupt_context(void){return irq;}static int ml_get_interrupts_enabled(void){return interrupts;}
static int get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,int*out,unsigned size){assert(!strcmp(n,"i9100xpcdiag")&&size==4);parses++;*out=enabled;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile unsigned*p){if(*p!=old)return 0;*p=value;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
''' + f'#include "{repo}/bsd/dev/arm/i9100_early_process_diag.h"\n' + r'''
int main(int argc,char**argv){
 assert(argc==2);struct proc p={14,14,(void*)0xc20e0000,"FixtureAgent"};
 i9100_early_process_note(&p,366);volatile uint32_t*r=&i9100_early_process_diag[4+14*12];
 assert(r[0]==1&&r[1]==14&&r[2]==14&&r[8]==366&&!strcmp((const char*)&r[4],"FixtureAgent"));
 unsigned old=parses;i9100_early_process_note(&p,20);assert(parses==old&&r[9]==1);
 p.p_idversion=15;strcpy(p.p_comm,"FixtureExec");i9100_early_process_note(&p,59);assert(r[2]==15&&r[9]==2&&!strcmp((const char*)&r[4],"FixtureExec"));
 for(unsigned n=0;n<6;n++){
  p.p_pid=15;irq=preemption=0;interrupts=enabled=1;
  switch(n){case 0:p.p_pid=0;break;case 1:p.p_pid=64;break;case 2:irq=1;break;case 3:interrupts=0;break;case 4:preemption=1;break;case 5:enabled=0;break;}
  i9100_early_process_note(&p,20);assert(!i9100_early_process_diag[4+15*12]);
 }
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_early_process_diag,1,sizeof(i9100_early_process_diag),f);fclose(f);
 puts("PASS production first64 bounds, name/version identity updates, unchanged fast path and IRQ/preemption/opt-in scope; proc lifetime modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'early-process.bin')],check=True);print('EVIDENCE',out)
