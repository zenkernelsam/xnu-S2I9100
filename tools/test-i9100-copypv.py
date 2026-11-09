#!/usr/bin/env python3
"""Exercise the production copy-direction/error contract without device access."""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys

if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,__file__],
              dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1]
source=(repo/'osfmk/arm/loose_ends.c').read_text()
start=source.index('kern_return_t copypv(');brace=source.index('{',start);end=brace+1;depth=1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
function=source[start:end]
out=repo/'out'/('copypv-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir(parents=True)
harness=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
typedef uint64_t addr64_t;
typedef uintptr_t user_addr_t;
typedef int kern_return_t;
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define cppvPsnk 1
#define cppvPsrc 2
#define cppvKmap 64
static unsigned in_calls,out_calls,direct_calls,maps,checks;
static int copy_error;
static unsigned char source[128],sink[128];
static jmp_buf invalid;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#v);exit(1);}}while(0)
static void panic(const char *s){(void)s;longjmp(invalid,1);}
static uintptr_t phys_to_virt(addr64_t address){maps++;if(address==0x40001000U)return (uintptr_t)source;if(address==0x40002000U)return (uintptr_t)sink;CHECK(0);return 0;}
static int copyin(user_addr_t src,void *dst,unsigned size){in_calls++;if(copy_error)return copy_error;memcpy(dst,(void *)src,size);return 0;}
static int copyout(const void *src,user_addr_t dst,unsigned size){out_calls++;if(copy_error)return copy_error;memcpy((void *)dst,src,size);return 0;}
static void test_bcopy(addr64_t src,addr64_t dst,unsigned size){direct_calls++;memmove((void *)(uintptr_t)dst,(void *)(uintptr_t)src,size);}
#define bcopy test_bcopy
''' + function + r'''
int main(void) {
 for(unsigned n=0;n<128;n++)source[n]=(n*7)^0x5a;
 for(unsigned flags=1;flags<128;flags++)if(flags&3) {
  unsigned in=(!(flags&64) && !(flags&2)),out=(!(flags&64) && !(flags&1));
  for(unsigned count=0;count<=128;count+=16)for(int error=0;error<=1;error++) {
   memset(sink,0xa5,sizeof(sink));in_calls=out_calls=direct_calls=maps=0;copy_error=error?14:0;
   addr64_t src=(flags&2)?0x40001000U:(uintptr_t)source;
   addr64_t dst=(flags&1)?0x40002000U:(uintptr_t)sink;
   int result=copypv(src,dst,count,flags);
   CHECK(in_calls==in && out_calls==out && direct_calls==(!in&&!out));
   CHECK(maps==!!(flags&1)+!!(flags&2));
   CHECK(result==((error&&(in||out))?KERN_FAILURE:KERN_SUCCESS));
   if(!error || (!in&&!out))CHECK(!memcmp(source,sink,count));
   else for(unsigned i=0;i<128;i++)CHECK(sink[i]==0xa5);
  }
 }
 if(setjmp(invalid)==0){copypv((uintptr_t)source,(uintptr_t)sink,1,0);CHECK(0);}
 CHECK(!memcmp(source,source,128));
 printf("PASS %u production direction/byte/error/flag checks\n",checks);
}
'''
(out/'test.c').write_text(harness)
for name,cmd in [('build',['gcc','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',out/'test.c','-o',out/'test']),('test',[out/'test'])]:
    result=subprocess.run(cmd,text=True,capture_output=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    if result.stdout:print(result.stdout,end='',flush=True)
print('Evidence:',out)
