#!/usr/bin/env python3
"""Exercise production VM metadata ownership and slab byte retention.

Syscall backends, atomic scheduling and self-task copyin are modeled. This
checks observation bounds/lifetime, not native VM allocation correctness.
"""
import os
from pathlib import Path
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
import subprocess
from datetime import datetime
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('backboard-vm-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
text=(repo/'bsd/kern/kern_sig.c').read_text()
start=text.index('static void i9100_ios6_slab_bytes(')
brace=text.index('{',start);end=brace+1;depth=1
while depth:
    depth += (text[end]=='{')-(text[end]=='}');end+=1
payload_function=text[start:end]
source=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t UInt32;typedef int32_t SInt32;typedef uint64_t user_addr_t;
static unsigned reject_cas;
static int OSCompareAndSwap(uint32_t old,uint32_t new,volatile uint32_t *p){
 if(reject_cas || *p!=old)return 0;*p=new;return 1;}
static int OSIncrementAtomic(volatile SInt32 *p){return ++*p;}
#define OSMemoryBarrier() ((void)0)
#define I9100_VM_OBSERVATION_IMPLEMENTATION 1
'''+f'#include "{repo}/pexpert/arm/i9100_vm_observation.h"\n#include "{repo}/pexpert/arm/i9100_abort_observation.h"\n'+r'''
volatile uint32_t i9100_ios6_slab_payload[2][12+1024];
static volatile UInt32 i9100_ios6_slab_payload_guard[2];
static unsigned copies,fail_read;static unsigned char memory[4096];
static int copyin(user_addr_t address,void *output,unsigned bytes){
 copies++;
 if(fail_read || address<0x1a9000 || address+bytes>0x1aa000)return 14;
 memcpy(output,memory+address-0x1a9000,bytes);return 0;
}
'''+payload_function+r'''
static void dump(const char *name,const void *data,unsigned bytes){
 FILE *f=fopen(name,"wb");assert(f && fwrite(data,1,bytes,f)==bytes);fclose(f);
}
int main(void){
 uint32_t raw[7]={0,4096,3,0x1002,0xffffffffU,0,0};
 uint64_t seq=i9100_vm_enter(8,0x123,197,0x392c1234,0x32ce23dc,0,4096,3,0x1002,0xffffffffU,0,0,raw,7);
 assert(seq==1);raw[1]=8192;
 i9100_vm_leave(seq,8,0x123,0,0x1a9000,0);
 assert(i9100_backboard_vm_diag[4+8]==4096 && i9100_backboard_vm_diag[4+21]==4096);
 assert(i9100_backboard_vm_diag[4+17]==0x1a9000 && i9100_backboard_vm_diag[4+16]==0);
 i9100_vm_leave(seq,8,0x123,22,0,0);assert(i9100_backboard_vm_diag[4+16]==0);
 assert(!i9100_vm_enter(9,0x123,73,0,0,0,0,0,0,0,0,0,raw,2));
 seq=i9100_vm_enter(8,0x123,73,0,0,0x1b9000,4096,0,0,0,0,0,raw,2);
 i9100_vm_leave(seq,9,0x123,22,0,0);assert(!i9100_backboard_vm_diag[4+28]);
 i9100_vm_leave(seq,8,0x123,22,0,0);assert(i9100_backboard_vm_diag[4+28+16]==22);
 seq=i9100_vm_enter(8,0x123,197,0,0,0,0,0,0,0,0,14,raw,7);
 i9100_vm_leave(seq,8,0x123,14,0,0);
 (void)i9100_vm_enter(8,0x456,75,0,0,0x1a9000,4096,0,5,0,0,0,raw,3);
 memset(memory,0x41,sizeof(memory));memset(memory,0,64);
 struct I9100AbortTrace trace={0};trace.pid=8;trace.task=0x80001000;
 trace.slab=0x1a9000;trace.slab_object=0x1fb456c0;trace.slab_expected=0xdeafbe03U;
 trace.frames[0]=0x39280973;
 i9100_ios6_slab_bytes(&trace,1);
 assert(i9100_ios6_slab_payload[1][1]==8 && i9100_ios6_slab_payload[1][7]==4096);
 assert(i9100_backboard_vm_diag[3]==1 && copies==1);
 trace.pid=58;i9100_ios6_slab_bytes(&trace,1);
 assert(i9100_ios6_slab_payload[1][1]==8 && copies==1);
 assert(!i9100_vm_enter(8,0x123,197,0,0,0,0,0,0,0,0,0,raw,7));
 dump("vm.bin",(const void*)i9100_backboard_vm_diag,sizeof(i9100_backboard_vm_diag));
 dump("payload.bin",(const void*)i9100_ios6_slab_payload,sizeof(i9100_ios6_slab_payload));
 memset((void*)i9100_ios6_slab_payload,0,sizeof(i9100_ios6_slab_payload));
 trace.pid=8;trace.slab=0x1a9001;i9100_ios6_slab_bytes(&trace,0);
 assert(i9100_ios6_slab_payload[0][7]==256);
 memset((void*)i9100_ios6_slab_payload,0,sizeof(i9100_ios6_slab_payload));
 fail_read=1;i9100_ios6_slab_bytes(&trace,0);
 assert(!i9100_ios6_slab_payload[0][7] && i9100_ios6_slab_payload[0][8]==14);
 memset((void*)i9100_ios6_slab_payload,0,sizeof(i9100_ios6_slab_payload));
 trace.slab=0x3fffff90U;unsigned old_copies=copies;i9100_ios6_slab_bytes(&trace,0);
 assert(copies==old_copies && i9100_ios6_slab_payload[0][8]==1);
 memset((void*)i9100_backboard_vm_diag,0,sizeof(i9100_backboard_vm_diag));
 memset((void*)i9100_ios6_slab_vm_diag,0,sizeof(i9100_ios6_slab_vm_diag));
 for(unsigned n=0;n<256;n++){
  seq=i9100_vm_enter(8,0x123,197,0,0,0,4096,3,0x1002,0xffffffffU,0,0,raw,7);
  assert(seq==n+1);i9100_vm_leave(seq,8,0x123,0,0x1a9000,0);
 }
 assert(!i9100_vm_enter(8,0x123,197,0,0,0,0,0,0,0,0,0,raw,7));
 assert(i9100_backboard_vm_diag[0]==256 && i9100_backboard_vm_diag[2]==1);
 // Dedicated calls remain recordable after the generic table is exhausted.
 unsigned slide=0x673000U;
 seq=i9100_vm_enter(8,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide,
                   0,4096,3,0x1002,0xffffffffU,0,0,raw,7);
 assert(seq==(1ULL<<32));i9100_vm_leave(seq,8,0x123,0,0x10b000,0);
 assert(i9100_ios6_slab_vm_diag[0]==1 && i9100_ios6_slab_vm_diag[4+17]==0x10b000);
 i9100_vm_leave(seq,8,0x123,22,0,0);assert(!i9100_ios6_slab_vm_diag[4+16]);
 assert(!i9100_vm_enter(9,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide,
                       0,4096,3,0x1002,0xffffffffU,0,0,raw,7));
 assert(!i9100_vm_enter(8,0x123,197,0x392ccc44U+slide,0x32ce23ddU+slide,
                       0,4096,3,0x1002,0xffffffffU,0,0,raw,7));
 assert(!i9100_vm_enter(8,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide+2U,
                       0,4096,3,0x1002,0xffffffffU,0,0,raw,7));
 for(unsigned n=0;n<7;n++){
  assert(!i9100_vm_enter(8,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide,
    n==0?4096:0,n==1?8192:4096,n==2?1:3,n==3?0x1012:0x1002,
    n==4?0:0xffffffffU,n==5?4096:0,n==6?14:0,raw,7));
 }
 assert(i9100_ios6_slab_vm_diag[0]==1);
 seq=i9100_vm_enter(8,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide,
                   0,4096,3,0x1002,0xffffffffU,0,0,raw,7);
 i9100_vm_leave(seq,8,0x456,12,0,0);assert(!i9100_ios6_slab_vm_diag[4+28]);
 i9100_vm_leave(seq,8,0x123,12,0,0);assert(i9100_ios6_slab_vm_diag[4+28+16]==12);
 (void)i9100_vm_enter(8,0x456,197,0x392ccc40U+slide,0x32ce23ddU+slide,
                     0,4096,3,0x1002,0xffffffffU,0,0,raw,7);
 dump("slab-vm.bin",(const void*)i9100_ios6_slab_vm_diag,sizeof(i9100_ios6_slab_vm_diag));
 for(unsigned n=3;n<64;n++){
  seq=i9100_vm_enter(8,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide,
                    0,4096,3,0x1002,0xffffffffU,0,0,raw,7);
  assert(seq==((uint64_t)(n+1)<<32));i9100_vm_leave(seq,8,0x123,0,0x10c000,0);
 }
 assert(!i9100_vm_enter(8,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide,
                       0,4096,3,0x1002,0xffffffffU,0,0,raw,7));
 assert(i9100_ios6_slab_vm_diag[0]==64 && i9100_ios6_slab_vm_diag[2]==1);
 i9100_backboard_vm_diag[3]=1;
 assert(!i9100_vm_enter(8,0x123,197,0x392ccc40U+slide,0x32ce23ddU+slide,
                       0,4096,3,0x1002,0xffffffffU,0,0,raw,7));
 memset((void*)i9100_backboard_vm_diag,0,sizeof(i9100_backboard_vm_diag));
 i9100_backboard_vm_diag[1]=8;reject_cas=1;
 assert(!i9100_vm_enter(8,0x123,197,0,0,0,0,0,0,0,0,0,raw,7));
 assert(i9100_backboard_vm_diag[0]==0 && i9100_backboard_vm_diag[2]==1);
 puts("PASS production VM metadata ownership/bounds and first slab bytes; backend/copyin/atomics modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True)
print('EVIDENCE',out)
