#!/usr/bin/env python3
"""Verify real ARM restart helper preserves SVC arguments and re-enters once.

Uses production C, an original iOS6 ARM SVC stub and a Thumb SVC fixture.
The kernel return/interrupt transport is modeled; no QEMU or phone.
"""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import json
import mmap
import struct
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('syscall-restart-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
text=(repo/'bsd/dev/arm/unix_syscalls.c').read_text();start=text.index('static void i9100_arm_syscall_restart(');brace=text.index('{',start);end=brace+1;depth=1
while depth:
    depth+=(text[end]=='{')-(text[end]=='}');end+=1
helper=text[start:end]
source=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct {uint32_t r[13],sp,lr,pc,cpsr;} arm_saved_state_t;
volatile uint32_t i9100_syscall_restart_diag[161];
struct proc {unsigned p_pid;};static struct proc process={158};static unsigned enabled=1;
static struct proc*current_proc(void){return &process;}
static int PE_parse_boot_argn(const char*n,void*out,unsigned size){assert(!strcmp(n,"i9100xpcdiag")&&size==4);*(unsigned*)out=enabled;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned next,volatile unsigned*p){if(*p!=old)return 0;*p=next;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
'''+helper+r'''
int main(int argc,char**argv){
 assert(argc==2);arm_saved_state_t s={0};for(unsigned n=0;n<13;n++)s.r[n]=0x1100+n;s.sp=0x2000;s.lr=0x3000;
 uint32_t original[13];memcpy(original,s.r,sizeof(original));
 s.pc=0x392cd0fc;s.cpsr=0xa0000010;i9100_arm_syscall_restart(&s,301);
 assert(s.pc==0x392cd0f8&&s.cpsr==0x80000010&&!memcmp(original,s.r,sizeof(original))&&s.sp==0x2000&&s.lr==0x3000);
 s.pc=0x1002;s.cpsr=0xe0000030;i9100_arm_syscall_restart(&s,3);
 assert(s.pc==0x1000&&s.cpsr==0xc0000030&&!memcmp(original,s.r,sizeof(original)));
 enabled=0;s.pc=0x8004;s.cpsr=0x20000010;i9100_arm_syscall_restart(&s,4);assert(s.pc==0x8000&&s.cpsr==0x10&&i9100_syscall_restart_diag[0]==2);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_syscall_restart_diag,1,sizeof(i9100_syscall_restart_diag),f);fclose(f);
 puts("PASS production ARM/Thumb restart PCs, C clear, all arguments/SP/LR unchanged, diagnostic opt-out");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'syscall-restarts.bin')],check=True)
sys.path.insert(0,str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_MODE_THUMB,UC_HOOK_INTR
from unicorn.arm_const import *
with (repo.parent/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as f:
    with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as cache:
        mo,mc=struct.unpack_from('<2I',cache,16);maps=[struct.unpack_from('<QQQII',cache,mo+n*32) for n in range(mc)]
        va=0x392cd0e4;mapping=next(r for r in maps if r[0]<=va<r[0]+r[1]);at=mapping[2]+va-mapping[0];arm=bytes(cache[at:at+28])
cases=[]
for name,base,code,mode,svc,width in [('original-iOS6-ARM',0x392cd0e4,arm,UC_MODE_ARM,0x392cd0f8,4),('Thumb-SVC',0x1000,bytes.fromhex('80df00bf'),UC_MODE_THUMB,0x1000,2)]:
    uc=Uc(UC_ARCH_ARM,mode);uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);uc.mem_map(base&~4095,4096);uc.mem_write(base,code)
    uc.mem_map(0x10000000,0x2000);uc.reg_write(UC_ARM_REG_SP,0x10001000);uc.mem_write(0x10001000,struct.pack('<2I',0x5566,0x7788))
    uc.reg_write(UC_ARM_REG_R0,0x1111);uc.reg_write(UC_ARM_REG_R1,0x2222);uc.reg_write(UC_ARM_REG_R2,0x3333);uc.reg_write(UC_ARM_REG_R3,0x4444)
    seen=[]
    def trap(uc,number,_):seen.append(dict(pc=uc.reg_read(UC_ARM_REG_PC),r0=uc.reg_read(UC_ARM_REG_R0),r4=uc.reg_read(UC_ARM_REG_R4),r5=uc.reg_read(UC_ARM_REG_R5),sp=uc.reg_read(UC_ARM_REG_SP)));uc.emu_stop()
    uc.hook_add(UC_HOOK_INTR,trap);uc.emu_start(base|int(mode==UC_MODE_THUMB),base+len(code),count=20)
    assert seen[-1]['pc']==svc+width
    before=dict(seen[-1]);uc.emu_start((seen[-1]['pc']-width)|int(mode==UC_MODE_THUMB),base+len(code),count=1)
    assert seen[-1]==before and len(seen)==2
    cases.append(dict(name=name,restarted_instruction=hex(svc),two_traps_identical=seen))
(out/'result.json').write_text(json.dumps(dict(passed=True,scope=__doc__,cases=cases),indent=2))
assert text.count('i9100_arm_syscall_restart(state,code);')==2
print('PASS original ARM SVC stub and Thumb SVC reentry; both production return paths use helper')
print('EVIDENCE',out)
