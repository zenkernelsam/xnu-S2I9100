#!/usr/bin/env python3
"""Verify production publication-point installation and actual ARM metrics code.

Syscall boundaries are modeled; original UIKit return instructions execute on
Cortex-A9. No claim about live compositor behavior or physical touch.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import struct,json,subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('ui-metrics-density-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
source=(repo/'bsd/dev/arm/i9100_ui_density.h').read_text()
metrics=(repo/'bsd/dev/arm/i9100_ui_density_metrics_code.h').read_text()+'\n'+(repo/'bsd/dev/arm/i9100_ui_density_metrics.h').read_text()
source=source.replace('#include "i9100_ui_density_metrics.h"',metrics)
(out/'production.h').write_text('\n'.join(x for x in source.splitlines() if not x.startswith('#include'))+'\n')
commands=[['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
    '-I'+str(out),str(repo/'port-tests/i9100_ui_metrics_test.c'),'-o',str(out/'check')],
    [str(out/'check'),str(out)],
    ['arm-none-eabi-as','-march=armv7-a','-mfpu=neon',str(repo/'port-tests/density-metrics-template.s'),'-o',str(out/'template.o')],
    ['arm-none-eabi-objcopy','-O','binary','-j','.text',str(out/'template.o'),str(out/'template.bin')]]
for n,cmd in enumerate(commands):
    r=subprocess.run(cmd,text=True,capture_output=True);(out/f'command-{n}.log').write_text(r.stdout+r.stderr)
    if r.returncode:print(r.stderr[-3500:],file=sys.stderr);r.check_returncode()
    if r.stdout:print(r.stdout.strip(),flush=True)
sys.path.insert(0,str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE,UC_HOOK_INTR
from unicorn.arm_const import *
plain=(out/'template.bin').read_bytes();assert len(plain)==276
gprs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,
      UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12]
cases=[]
for option in (1333,1500):
 for slide in (0,0x0375b000):
  code=(out/f'code-{option}-{slide:08x}.bin').read_bytes();normalized=bytearray(code)
  for offset in (64,114,138,156):normalized[offset:offset+8]=plain[offset:offset+8]
  assert normalized==plain,'Production metrics template differs from assembler'
  for alignment in (0,4):
   for scenario in range(6):
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    u.mem_map(0x100000,0x1000);u.mem_map(0x110000,0x10000);u.mem_map(0x180000,0x1000);u.mem_map(0x20000000,0x2000)
    u.mem_map(0x3a282000+slide,0x2000);u.mem_write(0x100000,code)
    self=0x20001000;sp=0x118000+alignment;ret=0x180001
    ratio=struct.unpack('<f',struct.pack('<I',0x3faaaaab if option==1333 else 0x3fc00000))[0]
    scale=ratio if scenario==1 else 1.0;width=480/ratio if scenario==1 else 1024.0 if scenario==3 else 480.0;height=800/ratio if scenario==1 else 800.0
    obj=bytearray(44);struct.pack_into('<I',obj,0,0x11111111 if scenario==2 else 0x397e9534+slide)
    struct.pack_into('<4f',obj,8,0,0,width,height);struct.pack_into('<2f',obj,24,scale,1.0)
    gs_scale=ratio if scenario==1 else 2.0 if scenario==4 else 1.0
    u.mem_write(self,bytes(obj));u.mem_write(0x3a282460+slide,struct.pack('<2I',640 if scenario==5 else 480,800))
    u.mem_write(0x3a283e7c+slide,struct.pack('<f',gs_scale))
    initial=[0xcccc0000+n*0x111 for n in range(13)];initial[4]=self
    caller=[0xdddd0000+n*0x111 for n in range(13)];saved_d8=0x3ff0000000000088
    frame=b'\0'*40+struct.pack('<Q',saved_d8)+struct.pack('<2I',caller[8],caller[10])+struct.pack('<5I',caller[4],caller[5],caller[6],caller[7],ret)
    u.mem_write(sp,frame);u.reg_write(UC_ARM_REG_CPSR,0x30|0xa8050000)
    u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000);u.reg_write(UC_ARM_REG_FPSCR,0x01000000)
    fp=[0x3ff0000000000000+n for n in range(32)]
    for n,v in enumerate(fp):u.reg_write(UC_ARM_REG_D0+n,v)
    for r,v in zip(gprs,initial):u.reg_write(r,v)
    u.reg_write(UC_ARM_REG_SP,sp);u.reg_write(UC_ARM_REG_LR,0x177771);observed=[]
    def intr(cpu,number,cookie):
        assert number==2;phase=cpu.reg_read(UC_ARM_REG_R0);pc=cpu.reg_read(UC_ARM_REG_PC)
        assert pc==0x100000+(22 if phase==1 else 248) and cpu.reg_read(UC_ARM_REG_R12)==20
        observed.append((phase,struct.unpack('<f',cpu.mem_read(self+24,4))[0]))
        for r in gprs[:4]+[UC_ARM_REG_R12]:cpu.reg_write(r,0xeeeeeeee)
        cpsr=cpu.reg_read(UC_ARM_REG_CPSR);cpu.reg_write(UC_ARM_REG_CPSR,(cpsr&~0xf80f0000)|0x40000000)
    def hook(cpu,pc,size,cookie):
        if pc==ret&~1:cpu.emu_stop()
    u.hook_add(UC_HOOK_INTR,intr);u.hook_add(UC_HOOK_CODE,hook);u.emu_start(0x100001,0,count=4096)
    result=bytes(u.mem_read(self,44));new_gs=struct.unpack('<f',u.mem_read(0x3a283e7c+slide,4))[0]
    assert len(observed)==2 and [x[0] for x in observed]==[1,2]
    if scenario==0:
        w,h,s=struct.unpack_from('<3f',result,16);assert abs(w-480/ratio)<.001 and abs(h-800/ratio)<.001 and s==ratio and new_gs==ratio
        assert result[:16]==obj[:16] and result[28:]==obj[28:]
    else:assert result==obj and new_gs==gs_scale
    assert u.reg_read(UC_ARM_REG_PC)==ret&~1 and u.reg_read(UC_ARM_REG_SP)==sp+76
    for n,r in enumerate(gprs):assert u.reg_read(r)==(caller[n] if n in (4,5,6,7,8,10) else initial[n]),(n,scenario)
    assert u.reg_read(UC_ARM_REG_LR)==0x177771 and u.reg_read(UC_ARM_REG_CPSR)&0xf80f0000==0xa8050000
    assert u.reg_read(UC_ARM_REG_FPSCR)==0x01000000
    expected_fp=fp[:];expected_fp[8]=saved_d8;assert [u.reg_read(UC_ARM_REG_D0+n) for n in range(32)]==expected_fp
    assert bytes(u.mem_read(sp,len(frame)))==frame
    cases.append(dict(option=option,slide=hex(slide),stack_alignment=alignment,scenario=scenario))
(out/'result.json').write_text(json.dumps(dict(result='PASS',cases=cases,scope='Original epilogue and production ARM body; syscall boundary modeled; hardware rendering not qualified'),indent=2)+'\n')
print(f'PASS {len(cases)} Cortex-A9 publication paths; same GS/UIKit ratio, both bounds, original return, GPR/APSR/FPSCR/VFP and guarded no-ops',flush=True)
print('EVIDENCE',out,flush=True)
