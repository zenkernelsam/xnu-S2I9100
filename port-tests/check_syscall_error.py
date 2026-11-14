#!/usr/bin/env python3
"""Show old/new ARM continuation error flags against original iOS6 SVC stub."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import json
import mmap
import struct
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('syscall-error-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
text=(repo/'bsd/dev/arm/unix_syscalls.c').read_text();start=text.index('static void i9100_arm_syscall_error(');brace=text.index('{',start);end=brace+1;depth=1
while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
source=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t r[13],sp,lr,pc,cpsr;} arm_saved_state_t;
'''+text[start:end]+r'''
int main(void){for(unsigned n=1;n<256;n++){
 arm_saved_state_t s={0};for(unsigned k=0;k<13;k++)s.r[k]=k+0x100;s.pc=0x392cd0fc;s.sp=0x2000;s.lr=0x3000;s.cpsr=0x80000010;
 arm_saved_state_t original=s;i9100_arm_syscall_error(&s,n);
 assert(s.r[0]==n&&s.cpsr==0xa0000010&&!memcmp(s.r+1,original.r+1,12*4)&&s.pc==original.pc&&s.sp==original.sp&&s.lr==original.lr);
 }puts("PASS production error Carry and errno, r1-r12/PC/SP/LR preserved for255 errors");}
'''
(out/'check.c').write_text(source);subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined',str(out/'check.c'),'-o',str(out/'check')],check=True);subprocess.run([str(out/'check')],check=True)
sys.path.insert(0,str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
with (repo.parent/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as f:
    with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as d:
        mo,mc=struct.unpack_from('<2I',d,16);maps=[struct.unpack_from('<QQQII',d,mo+n*32) for n in range(mc)]
        def original(address,n):
            va,size,offset,*_=next(r for r in maps if r[0]<=address and address+n<=r[0]+r[1]);return bytes(d[offset+address-va:offset+address-va+n])
        stub=original(0x392cd0fc,32)
cases=[]
for label,carry in [('old-continuation',0),('correct-error',1)]:
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);uc.mem_map(0x392cd000,4096);uc.mem_write(0x392cd0fc,stub)
    uc.mem_map(0x10000000,4096);uc.mem_write(0x10000000,struct.pack('<2I',0x4444,0x5555))
    uc.reg_write(UC_ARM_REG_CPSR,0x10|(carry<<29));uc.reg_write(UC_ARM_REG_SP,0x10000000);uc.reg_write(UC_ARM_REG_R0,4);target=[]
    def stop(uc,at,size,_):
        if at in (0x392cd104,0x392cd118):target.append(at);uc.emu_stop()
    uc.hook_add(UC_HOOK_CODE,stop);uc.emu_start(0x392cd0fc,0x392cd11c,count=5)
    assert target==[0x392cd118 if carry==0 else 0x392cd104]
    cases.append(dict(label=label,carry=carry,stub_target=hex(target[0]),errno_veneer_reached=bool(carry)))
assert 'i9100_arm_syscall_error(state,error);' in text[text.index('void unix_syscall_return(int error)'):]
(out/'result.json').write_text(json.dumps(dict(passed=True,scope=__doc__,cases=cases),indent=2))
print('PASS original iOS6 ARM SVC branch: old error looks successful, corrected Carry enters errno veneer')
print('EVIDENCE',out)
