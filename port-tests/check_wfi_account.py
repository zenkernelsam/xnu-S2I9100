#!/usr/bin/env python3
"""Run the actual linked ARM machine_idle with instrumented WFI/MMIO boundaries.

WFI advances a modeled MCT counter; all original branch/load/store/ABI and
IRQ instructions execute on Cortex-A9. This is not physical power evidence.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
import argparse,importlib.util,struct,json
from pathlib import Path
from datetime import datetime
repo=Path(__file__).resolve().parents[1];sys.path.insert(0,str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
s=importlib.util.spec_from_file_location('wfi_symbols',repo.parent/'uniloader-S2I9100/scripts/i9100/live-memory.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--kernel',type=Path,required=True);a=p.parse_args()
kernel=a.kernel.resolve();assert kernel.is_relative_to(repo/'out');d=kernel.read_bytes();sy=m.Symbols(kernel)
out=repo/'out'/('wfi-account-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
segments=[];at=28
for _ in range(struct.unpack_from('<I',d,16)[0]):
    cmd,n=struct.unpack_from('<2I',d,at)
    if cmd==1:segments.append(struct.unpack_from('<4I',d,at+24))
    if cmd==2:symbol_offset,symbol_count,string_offset,string_bytes=struct.unpack_from('<4I',d,at+8)
    at+=n
for n in range(symbol_count):
    index,kind,section,description,value=struct.unpack_from('<IBBHI',d,symbol_offset+n*12)
    if index>=string_bytes:continue
    name=d[string_offset+index:d.find(b'\0',string_offset+index)].decode('utf-8','replace')
    if 'non_lazy_ptr' in name and ('idle' in name or 'power_save' in name):sy.values[name]=value
def bytes_at(va,n):
    off=next(off+va-start for start,size,off,fs in segments if start<=va and va+n<=start+fs)
    return d[off:off+n]
pcs=sy['_machine_idle'];size=(sy['.Lmachine_idle_return']&~1)+4-pcs
assert size<512
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
instructions=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(bytes_at(pcs,size),pcs));references=[];constants={}
for ins in instructions:
    if ins.mnemonic in ('movw','movt'):
        reg,value=ins.op_str.split(', #');number=int(value,0)
        constants[reg]=number if ins.mnemonic=='movw' else constants.get(reg,0)|(number<<16)
    if ins.mnemonic=='ldr' and ', [pc, ' in ins.op_str:
        register=ins.op_str.split(', [pc, ')[1].split(']')[0]
        references.append(ins.address+8+constants[register])
assert len(references)==3
gprs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12]
cases=[]
for enabled,mapped,old,delta,total in ((0,1,10,240000,0),(1,0,10,240000,0),(1,1,10,240000,0),
    (1,1,0xfffffff0,128,0xffffffff-30),(1,1,10,0,0)):
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);pages={}
    def map_page(page):
        if page not in pages:u.mem_map(page,4096);pages[page]=1
    begin=pcs&~4095;map_page(begin);u.mem_write(pcs,bytes_at(pcs,size))
    map_page(0x100000);map_page(0x110000);map_page(0x12000000)
    variables=[sy['_do_power_save'],sy['_i9100_idle_mct_map'],sy['_i9100_idle_wait_diag']]
    for ptr,va in zip(references,variables):
        assert struct.unpack('<I',bytes_at(ptr,4))[0]==va,'Original non-lazy reference differs'
        map_page(ptr&~4095);map_page(va&~4095);map_page((va+32)&~4095)
        u.mem_write(ptr,struct.pack('<I',va))
    u.mem_write(sy['_do_power_save'],struct.pack('<I',enabled));u.mem_write(sy['_i9100_idle_mct_map'],struct.pack('<I',0x12000000 if mapped else 0))
    raw=struct.pack('<8I',1,0,0,total&0xffffffff,total>>32,0,0,0)
    u.mem_write(sy['_i9100_idle_wait_diag'],raw);u.mem_write(0x12000100,struct.pack('<I',old))
    regs=[0x12340000+i*0x111 for i in range(13)];sp=0x110f00;lr=0x100000
    u.reg_write(UC_ARM_REG_CPSR,0x13)
    for r,v in zip(gprs,regs):u.reg_write(r,v)
    u.reg_write(UC_ARM_REG_SP,sp);u.reg_write(UC_ARM_REG_LR,lr);waits=[]
    def hook(cpu,pc,n,cookie):
        if pc==lr:cpu.emu_stop();return
        instruction=struct.unpack('<I',cpu.mem_read(pc,4))[0]
        # Unicorn masks only I for this CPS encoding. Treat the privileged
        # instruction as an explicit facade, checking its real machine word.
        if instruction in (0xf10c00c0,0xf10800c0):
            mask=cpu.reg_read(UC_ARM_REG_CPSR)
            cpu.reg_write(UC_ARM_REG_CPSR,mask|0xc0 if instruction==0xf10c00c0 else mask&~0xc0)
            cpu.reg_write(UC_ARM_REG_PC,pc+4);return
        if instruction==0xe320f003:
            assert cpu.reg_read(UC_ARM_REG_CPSR)&0xc0==0xc0,(enabled,mapped,hex(pc),hex(cpu.reg_read(UC_ARM_REG_CPSR)))
            waits.append(pc);cpu.mem_write(0x12000100,struct.pack('<I',(old+delta)&0xffffffff));cpu.reg_write(UC_ARM_REG_PC,pc+4)
    u.hook_add(UC_HOOK_CODE,hook);u.emu_start(pcs,0,count=2048)
    assert u.reg_read(UC_ARM_REG_PC)==lr and u.reg_read(UC_ARM_REG_SP)==sp and u.reg_read(UC_ARM_REG_LR)==lr
    assert [u.reg_read(r) for r in gprs[4:]]==regs[4:]
    words=struct.unpack('<8I',u.mem_read(sy['_i9100_idle_wait_diag'],32))
    if enabled and mapped:
        assert len(waits)==1 and words[1]==2 and words[2]==1 and (words[4]<<32|words[3])==total+delta
        assert words[5]==delta and words[6]==delta and u.reg_read(UC_ARM_REG_CPSR)&0xc0==0
    else:assert len(waits)==bool(enabled) and words==struct.unpack('<8I',raw)
    cases.append(dict(enabled=enabled,mapped=mapped,before=old,delta=delta,total=total))
(out/'result.json').write_text(json.dumps(dict(result='PASS',kernel_sha=sy.tag,cases=cases,limit='WFI/MMIO/CPS facade modeled; actual ARM ABI and counters run; no physical WFI timing evidence'),indent=2)+'\n')
print('PASS linked ARM machine_idle: actual wait counters/64-bit carry/MCT wrap, WFI masked, IRQ behavior and callee-saved registers')
print('EVIDENCE',out)
