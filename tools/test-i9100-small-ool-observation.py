#!/usr/bin/env python3
"""Actual small-copy observer scope/phase tests; copyin and maps modeled."""
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1];s=(repo/'osfmk/vm/vm_map.c').read_text();start=s.index('static void i9100_small_ool_note(');fn=s[start:s.index('\n#endif',start)]
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UInt32,vm_map_address_t;typedef int kern_return_t;
typedef void *vm_map_t;struct cp{unsigned type,size;};typedef struct cp *vm_map_copy_t;
struct th{vm_map_t map;};typedef struct th *thread_t;
static struct th thread={(void*)0x1000};static unsigned enabled=1,irq,preemption,interrupts=1,busy,checks,reads;
static const char *name="installd";static unsigned char source[543];
static volatile uint32_t i9100_small_ool_diag[2561],i9100_small_ool_guard;
static thread_t current_thread(void){return &thread;}static vm_map_t current_map(void){return thread.map;}
static int get_preemption_level(void){return preemption;}static int ml_get_interrupts_enabled(void){return interrupts;}static int ml_at_interrupt_context(void){return irq;}
static int PE_parse_boot_argn(const char *p,void *out,unsigned n){(void)p;(void)n;*(int*)out=enabled;return 1;}
static int proc_selfpid(void){return 45;}static void proc_name(int p,char *out,unsigned n){(void)p;strncpy(out,name,n-1);out[n-1]=0;}
static int OSCompareAndSwap(unsigned a,unsigned b,volatile uint32_t *p){if(busy||*p!=a)return 0;*p=b;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int copyin(unsigned address,void *out,unsigned n){++reads;if(address!=0x2000||n>sizeof(source))return 1;memcpy(out,source,n);return 0;}
static void check(int ok,const char *why){++checks;if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
'''
tail=r'''
int main(void){struct cp copy={3,543};unsigned char kernel[543]={0};memset(source,'x',sizeof(source));
 i9100_small_ool_note(0,thread.map,0x2000,&copy,0,0);check(i9100_small_ool_diag[17]=='x'*0x01010101U && i9100_small_ool_diag[12]==256,"source bytes retained independently");
 i9100_small_ool_note(1,thread.map,0x2000,&copy,kernel,0);unsigned at=1+80;check(i9100_small_ool_diag[at+11]==256 && i9100_small_ool_diag[at+16]==0,"empty kernel buffer distinguishable");
 memcpy(kernel,source,sizeof(kernel));i9100_small_ool_note(2,thread.map,0x2000,&copy,kernel,0);at=1+160;check(i9100_small_ool_diag[at+16]=='x'*0x01010101U,"pre-copyout backing bytes");
 i9100_small_ool_note(3,(void*)0x3000,0x2000,&copy,0,0);at=1+240;check(i9100_small_ool_diag[at+12]==2 && !i9100_small_ool_diag[at+11],"foreign map not read after restore");
 unsigned old=reads,seq=i9100_small_ool_diag[0];irq=1;i9100_small_ool_note(0,thread.map,0x2000,&copy,0,0);check(reads==old&&i9100_small_ool_diag[0]==seq,"IRQ gate");irq=0;
 preemption=1;i9100_small_ool_note(0,thread.map,0x2000,&copy,0,0);check(reads==old,"preemption gate");preemption=0;
 enabled=0;i9100_small_ool_note(0,thread.map,0x2000,&copy,0,0);check(i9100_small_ool_diag[0]==seq,"explicit enable");enabled=1;
 copy.size=544;i9100_small_ool_note(0,thread.map,0x2000,&copy,0,0);check(i9100_small_ool_diag[0]==seq,"exact witnessed size");copy.size=543;
 busy=1;i9100_small_ool_note(0,thread.map,0x2000,&copy,0,0);check(reads==old,"busy observer not blocking");busy=0;
 name="Other";i9100_small_ool_note(0,thread.map,0x2000,&copy,0,0);check(i9100_small_ool_diag[0]==seq,"process scope");name="lsd";
 i9100_small_ool_note(3,thread.map,0x3fffffff,&copy,0,0);at=1+(seq&31)*80;check(i9100_small_ool_diag[at+12]==2 && reads==old,"user range checked");
 printf("%u production small-copy observer checks passed; copyin/map boundary modeled\n",checks);}
'''
out=repo/'out'/('small-ool-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir(parents=True);c=out/'small.c';b=out/'small';c.write_text(prefix+fn+tail)
subprocess.run(['clang','-std=c11','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(b)],check=True);subprocess.run([str(b)],check=True);print(out,flush=True)
