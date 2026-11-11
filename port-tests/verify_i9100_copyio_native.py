#!/usr/bin/env python3
"""Run the deployed ARM copyin/out instructions; MMU/fault handling is modeled."""
import os
from pathlib import Path
import struct
import sys
import argparse
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_UNMAPPED
from unicorn.arm_const import UC_CPU_ARM_CORTEX_A9,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_CPSR
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--kernel',type=Path,required=True);a=parser.parse_args()
d=a.kernel.read_bytes();segments=[];pos=28;symbols={};sym=None
for n in range(struct.unpack_from('<I',d,16)[0]):
    c,z=struct.unpack_from('<2I',d,pos)
    if c==1:segments.append(struct.unpack_from('<4I',d,pos+24))
    if c==2:sym=struct.unpack_from('<4I',d,pos+8)
    pos+=z
for n in range(sym[1]):
    name,t,s,desc,value=struct.unpack_from('<IBBHI',d,sym[0]+n*12)
    if name<sym[3] and value:
        at=sym[2]+name;label=d[at:at+256].split(b'\0',1)[0].decode('ascii','replace');symbols[label]=value
USER,KERNEL,STACK,THREAD,STOP=0x10000000,0x90000000,0x91000000,0x92000000,0x93000000
uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
for address in (USER,KERNEL,STACK,THREAD,STOP):uc.mem_map(address,0x20000)
def page(uc,access,address,size,value,data):
    base=address&~4095
    for v,z,o,f in segments:
        if v<=base and base+4096<=v+z:
            uc.mem_map(base,4096);at=o+base-v;available=max(0,min(4096,f-(base-v)))
            if available:uc.mem_write(base,d[at:at+available])
            return True
    return False
def code(uc,address,size,data):
    if address==STOP:uc.emu_stop();return
    # LoadThreadRegister: TPIDRPRW is an external boundary in this CPU model.
    op=struct.unpack('<I',uc.mem_read(address,4))[0]
    if op==0xee1daf90:
        uc.reg_write(UC_ARM_REG_R10,THREAD);uc.reg_write(UC_ARM_REG_PC,address+4)
uc.hook_add(UC_HOOK_MEM_UNMAPPED,page);uc.hook_add(UC_HOOK_CODE,code)
checks=0
for label in ('_copyin','_copyout'):
    for length in (1,2,3,4,7,8,31,32,33,127,128,129,543,1023,4096):
        for source_offset in range(4):
            for destination_offset in range(4):
                src=(USER if label=='_copyin' else KERNEL)+4096+source_offset
                dst=(KERNEL if label=='_copyin' else USER)+4096+destination_offset
                payload=bytes((i*17+39)&255 for i in range(length))
                uc.mem_write(src,payload);uc.mem_write(dst-8,b'\xa5'*(length+16))
                uc.reg_write(UC_ARM_REG_CPSR,0x13);uc.reg_write(UC_ARM_REG_SP,STACK+0x1fff0)
                uc.reg_write(UC_ARM_REG_LR,STOP);uc.reg_write(UC_ARM_REG_R0,src);uc.reg_write(UC_ARM_REG_R1,dst);uc.reg_write(UC_ARM_REG_R2,length)
                uc.emu_start(symbols[label],STOP,count=200000)
                assert uc.reg_read(UC_ARM_REG_PC)==STOP,(label,length,source_offset,destination_offset,'did not return',hex(uc.reg_read(UC_ARM_REG_PC)))
                assert uc.reg_read(UC_ARM_REG_R0)==0,(label,length,'copy result')
                assert bytes(uc.mem_read(dst,length))==payload,(label,length,source_offset,destination_offset,'bytes')
                assert bytes(uc.mem_read(dst-8,8))==b'\xa5'*8 and bytes(uc.mem_read(dst+length,8))==b'\xa5'*8,(label,length,'bounds')
                checks+=1
print(f'{checks} actual ARM copyin/out byte/alignment cases passed; MMU/fault handling modeled')
