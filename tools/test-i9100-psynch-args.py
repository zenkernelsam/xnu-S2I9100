#!/usr/bin/env python3
"""Compile the production ARM argument helpers against captured iOS6 inputs.

No device access. Retains native sanitizer programs and logs in a new out dir.
"""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys


def extract(source, name):
    pos=source.index(name+'(')
    begin=source.rfind('static',0,pos)
    brace=source.index('{',pos);depth=1;end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        if source[end]=='}':depth-=1
        end+=1
    return source[begin:end]+'\n'


def main():
    if os.environ.get('I9100_CPU_CAPPED')!='1':
        os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                             sys.executable,__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
    repo=Path(__file__).resolve().parents[1]
    out=repo/'out'/('psynch-args-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    text=(repo/'bsd/dev/arm/unix_syscalls.c').read_text()
    functions='\n'.join(extract(text,n) for n in ('arm_syscall_store_args','ios7lab_psynch_stack_tail','arm_psynch_needs_stack_tail'))
    harness=r'''
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
typedef int boolean_t;
typedef uint32_t user_addr_t;
#define FALSE 0
#define TRUE 1
static unsigned checks,copy_calls;
static int vmabi,copy_error;
static uint32_t userSP=0x2039ed9c,stack[11];
static int ios7_compat_vmabi_enabled(void){return vmabi;}
static int copyin(user_addr_t addr,void *dst,unsigned size){
 copy_calls++;
 if(copy_error)return EFAULT;
 if(addr!=userSP+28 || size!=16)return EFAULT;
 memcpy(dst,stack+7,size);return 0;
}
#define CHECK(c) do {checks++;if(!(c)){printf("FAIL %u: %s\n",__LINE__,#c);return 1;}}while(0)
'''+functions+r'''
int main(void){
 /* All three real iOS6 stubs begin MOV ip,sp; PUSH r4/r5/r6/r8;
  * LDM ip,r4/r5/r6. Four saved plus three loaded words = tail at SP+28. */
 unsigned prologue[]={0xe1a0c00d,0xe92d0170,0xe89c0070};
 CHECK((prologue[1]&0xffff)==0x170 && (prologue[2]&0xffff)==0x70);
 CHECK(__builtin_popcount(prologue[1]&0xffff)*4+__builtin_popcount(prologue[2]&0xffff)*4==28);
 for(vmabi=0;vmabi<=1;vmabi++){
  for(unsigned code=301;code<=307;code++){
   unsigned selected=code>=303&&code<=305;
   CHECK(arm_psynch_needs_stack_tail(code,FALSE)==(selected&&(BOARD_CONFIG_EXYNOS4210_I9100||vmabi)));
   CHECK(!arm_psynch_needs_stack_tail(code,TRUE));
  }
 }
 /* Captured mediaserverd and SpringBoard cvwait tails. The old register
  * copy used the FP as flags and r10=1 as nsec, not these stack words. */
 uint32_t regs[13]={0x1c53ef64,0x04b27c00,0x04b27b00,0,0,0,0,0x2039ee18,0,0,1,0,305};
 const uint32_t flags[]={0x10068,0x60};
 for(unsigned f=0;f<2;f++)for(unsigned code=303;code<=305;code++){
  uint32_t args[13];for(unsigned i=0;i<13;i++)args[i]=0xfeedface;
  for(unsigned i=0;i<11;i++)stack[i]=0x11223300+i;
  stack[7]=flags[f];stack[8]=0;stack[9]=0;stack[10]=0;
  CHECK(!arm_syscall_store_args(args,12,regs,11,FALSE));
  CHECK(args[7]==regs[7] && args[10]==1);
  CHECK(!ios7lab_psynch_stack_tail(code,userSP,args,11));
  for(unsigned i=0;i<7;i++)CHECK(args[i]==regs[i]);
  for(unsigned i=7;i<11;i++)CHECK(args[i]==stack[i]);
  CHECK(args[11]==0xfeedface && args[12]==0xfeedface);
 }
 uint32_t args[12],before[12];for(unsigned i=0;i<12;i++)args[i]=before[i]=0x12340000+i;
 unsigned calls=copy_calls;
 CHECK(ios7lab_psynch_stack_tail(305,userSP,args,10)==EINVAL);
 CHECK(copy_calls==calls && !memcmp(args,before,sizeof(args)));
 CHECK(ios7lab_psynch_stack_tail(305,userSP+1,args,11)==EFAULT);
 CHECK(copy_calls==calls);
 CHECK(ios7lab_psynch_stack_tail(305,0xfffffff0,args,11)==EFAULT);
 CHECK(copy_calls==calls);
 copy_error=1;CHECK(ios7lab_psynch_stack_tail(305,userSP,args,11)==EFAULT);
 CHECK(!memcmp(args,before,sizeof(args)));
 CHECK(arm_syscall_store_args(args,12,regs,13,FALSE)==EINVAL);
 CHECK(!memcmp(args,before,sizeof(args)));
 printf("psynch argument ABI: %u checks, native=%d\n",checks,BOARD_CONFIG_EXYNOS4210_I9100);
 return 0;
}
'''
    source=out/'harness.c';source.write_text(harness)
    for native in (0,1):
        binary=out/('test-native-'+str(native))
        command=['gcc','-std=c99','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                 '-DBOARD_CONFIG_EXYNOS4210_I9100='+str(native),str(source),'-o',str(binary)]
        compiled=subprocess.run(command,text=True,capture_output=True)
        (out/('build-'+str(native)+'.log')).write_text(compiled.stdout+compiled.stderr)
        if compiled.returncode:raise RuntimeError(compiled.stderr)
        result=subprocess.run([binary],text=True,capture_output=True)
        (out/('test-'+str(native)+'.log')).write_text(result.stdout+result.stderr)
        print(result.stdout.strip(),flush=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    print('PASS production ARM helpers; evidence:',out)


if __name__=='__main__':main()
