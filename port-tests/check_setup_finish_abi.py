#!/usr/bin/env python3
"""Verify production delayed-finish ARM arguments and original run registers.

Only the native NSObject call boundary is modeled. The original run prologue
executes on Cortex-A9; no claim about live timer dispatch or setup completion.
"""
import argparse, hashlib, os, struct, subprocess, sys
from pathlib import Path
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
out=a.evidence.resolve()
assert out.is_relative_to(repo/'out') and (out/'code.bin').is_file()
for command in ([ 'arm-none-eabi-as','-march=armv7-a','-mfpu=neon',str(repo/'port-tests/setup-finish-template.s'),'-o',str(out/'template.o')],
                [ 'arm-none-eabi-objcopy','-O','binary','-j','.text',str(out/'template.o'),str(out/'template.bin')]):
    result=subprocess.run(command,text=True,capture_output=True);(out/(Path(command[0]).name+'.log')).write_text(result.stdout+result.stderr);result.check_returncode()
code=(out/'code.bin').read_bytes();plain=(out/'template.bin').read_bytes();assert len(code)==len(plain)==132
normalized=bytearray(code)
for off in (32,42,60,72):normalized[off:off+8]=plain[off:off+8]
normalized[116:124]=b'\0'*8;normalized[128:132]=b'\0'*4
assert normalized==plain,'Production template differs from actual assembler'
gprs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,
      UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12]
cases=0
for flags in (0,0x20000000,0xa8050000,0xf80f0000):
  for null_ref in (None,1,2):
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    u.mem_map(0x10000,0x70000);u.mem_map(0x100000,0x10000);u.mem_map(0x20000000,0x1000)
    u.mem_write(0x20000000,code);u.mem_write(0x70068,struct.pack('<I',0 if null_ref==1 else 0x78000))
    u.mem_write(0x6f430,struct.pack('<I',0 if null_ref==2 else 0x78100))
    initial=[0x77000]+[0x11110000+i*0x101 for i in range(1,13)];sp=0x108000;lr=0x7fff1
    u.reg_write(UC_ARM_REG_CPSR,0x30|flags);u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
    u.reg_write(UC_ARM_REG_FPSCR,0x01000000);fp=[0x3ff0000000000000+n for n in range(32)]
    for n,v in enumerate(fp):u.reg_write(UC_ARM_REG_D0+n,v)
    for r,v in zip(gprs,initial):u.reg_write(r,v)
    u.reg_write(UC_ARM_REG_SP,sp);u.reg_write(UC_ARM_REG_LR,lr);calls=[]
    def hook(cpu,pc,size,cookie):
        if pc==0x63f10:
            call=[cpu.reg_read(r) for r in gprs[:4]]
            assert call==[initial[0],0x78000,0x78100,0]
            current_sp=cpu.reg_read(UC_ARM_REG_SP);assert current_sp%8==0
            assert struct.unpack('<d',cpu.mem_read(current_sp,8))[0]==5.0
            calls.append(call)
            for r in gprs[:4]+[UC_ARM_REG_R12]:cpu.reg_write(r,0xeeeeeeee)
            for n in list(range(8))+list(range(16,32)):cpu.reg_write(UC_ARM_REG_D0+n,0xcccccccccccccccc)
            cpu.reg_write(UC_ARM_REG_FPSCR,0)
            cpsr=cpu.reg_read(UC_ARM_REG_CPSR);cpu.reg_write(UC_ARM_REG_CPSR,(cpsr&0x0ff0ffff)|0x40000000)
            cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR));return
        if pc==0x12774:cpu.emu_stop()
    u.hook_add(UC_HOOK_CODE,hook);u.emu_start(0x20000001,0,count=4096)
    assert len(calls)==(null_ref is None)
    assert u.reg_read(UC_ARM_REG_PC)==0x12774 and u.reg_read(UC_ARM_REG_SP)==sp-32
    for n,r in enumerate(gprs):assert u.reg_read(r)==(sp-8 if n==7 else initial[n]),(n,null_ref)
    assert u.reg_read(UC_ARM_REG_LR)==lr and u.reg_read(UC_ARM_REG_CPSR)&0xf80f0000==flags
    assert u.reg_read(UC_ARM_REG_FPSCR)==0x01000000
    assert [u.reg_read(UC_ARM_REG_D0+n) for n in range(32)]==fp
    assert struct.unpack('<3I',u.mem_read(sp-32,12))==(initial[8],initial[10],initial[11])
    assert struct.unpack('<5I',u.mem_read(sp-20,20))==(initial[4],initial[5],initial[6],initial[7],lr)
    cases+=1
(out/'abi-result.txt').write_text(f'PASS {cases} Cortex-A9 paths; production/assembler match; native call arguments, nil refs, GPR/APSR/FPSCR/VFP and original run prologue\n')
print((out/'abi-result.txt').read_text().strip());print('CODE_SHA256',hashlib.sha256(code).hexdigest())
