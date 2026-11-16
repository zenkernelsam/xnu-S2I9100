#!/usr/bin/env python3
"""Check production background bytes against an assembler and original APIs.

Original UIView/UIImageView instructions run on Cortex-A9. Objective-C calls
to CALayer bounds/geometry are modeled boundaries, not a live compositor.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
import argparse,importlib.util,json,mmap,struct,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[1];root=repo.parent
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--producer',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();producer=a.producer.resolve()
    if not out.is_relative_to(repo/'out') or out.exists() or not producer.is_relative_to(repo/'out'):p.error('Project producer and fresh XNU output required')
    out.mkdir()
    subprocess.run(['arm-none-eabi-as','-march=armv7-a','-mfpu=neon',repo/'port-tests/ui-background-template.s','-o',out/'template.o'],check=True)
    subprocess.run(['arm-none-eabi-objcopy','-O','binary','-j','.text',out/'template.o',out/'template.bin'],check=True)
    asm=(out/'template.bin').read_bytes();assert len(asm)==172
    patched=set()
    for at in (44,52,64,92,104,114):patched.update(range(at,at+8))
    patched.update(range(156,164));patched.update(range(168,172))
    rs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_CPSR]
    ds=[globals()['UC_ARM_REG_D'+str(i)] for i in range(32)]
    s=importlib.util.spec_from_file_location('bg_cache',root/'uniloader-S2I9100/scripts/i9100/symbolize-ios6-stacks.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
    reference=(root/'research/ios6-setup-original-20261116-0724/Setup').read_bytes()
    # This original main image maps its initial __TEXT file at VA 0x1000.
    original=reference[0x22b4:0x22b4+10];assert original==bytes.fromhex('4cf65470c0f205007844')
    results=[]
    with (root/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as f:
        with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
            c=m.Cache(data)
            for slide,header,suffix in ((0,0x65000,'.background'),(0x20c000,0x2d000,'.background-slide')):
                code=(producer/('code.bin'+suffix)).read_bytes();assert len(code)==172
                assert all(code[i]==asm[i] for i in range(172) if i not in patched),'Production code differs from independent assembler'
                for rectangle in ((0.,0.,480.,800.),(3.,4.,320.,480.)):
                    for child,parent in ((0x15001000,0x15002000),(0,0x15002000),(0x15001000,0)):
                        comparisons=[];calls=[]
                        for enabled in (False,True):
                            u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
                            pages=set()
                            def mapping(address,size=4096):
                                for pg in range(address&~4095,(address+size+4095)&~4095,4096):
                                    if pg not in pages:u.mem_map(pg,4096);pages.add(pg)
                            mapping(0x1000);mapping(0x15000000,65536);mapping(0x20000000,131072)
                            site=header+0x22b4;mapping(site,32)
                            for address,size in ((0x32f33658,48),(0x32f3372c,48),(0x32f51400,24)):
                                mapping(address+slide,size);off=c.offset(address,size);u.mem_write(address+slide,data[off:off+size])
                            for address,value in ((0x397efe24,4),(0x397efe28,40),(0x397d83fc,0x333a5768+slide),(0x397dc310,0x333c8afb+slide)):
                                mapping(address+slide);u.mem_write(address+slide,struct.pack('<I',value))
                            for address in (0x333a0aa4+slide,0x333a0ad4+slide):mapping(address);u.mem_write(address,b'\x1e\xff\x2f\xe1')
                            u.mem_write(site,struct.pack('<2I',0xf000f8df,0x12c1) if enabled else original[:8]);u.mem_write(site+8,original[8:]);u.mem_write(0x12c0,code)
                            u.mem_write(0x15002004,struct.pack('<I',0x15003000));u.mem_write(0x15001028,struct.pack('<I',0x89abcdef))
                            u.mem_write(0x20010000,b'\x5a'*128)
                            u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
                            u.reg_write(UC_ARM_REG_CPSR,0xb80f0030);u.reg_write(UC_ARM_REG_FPSCR,0x00c80013)
                            for i,r in enumerate(rs[:13]):u.reg_write(r,0x7000+i)
                            u.reg_write(UC_ARM_REG_R0,child);u.reg_write(UC_ARM_REG_R2,child);u.reg_write(UC_ARM_REG_R8,parent)
                            u.reg_write(UC_ARM_REG_SP,0x20010000);u.reg_write(UC_ARM_REG_LR,0x2001ff01)
                            for i,r in enumerate(ds):u.reg_write(r,0x4000000012340000+i)
                            observed=[]
                            def boundary(uc,address,size,unused):
                                if address==site+10:uc.emu_stop();return
                                if address==0x333a0ad4+slide:
                                    assert uc.reg_read(UC_ARM_REG_R1)==0x15003000 and uc.reg_read(UC_ARM_REG_R2)==0x333a5768+slide
                                    uc.mem_write(uc.reg_read(UC_ARM_REG_R0),struct.pack('<4f',*rectangle));observed.append('original UIView bounds -> modeled CALayer')
                                elif address==0x333a0aa4+slide:
                                    sp=uc.reg_read(UC_ARM_REG_SP)
                                    values=(uc.reg_read(UC_ARM_REG_R2),uc.reg_read(UC_ARM_REG_R3),*struct.unpack('<2I',uc.mem_read(sp,8)))
                                    assert struct.unpack('<4f',struct.pack('<4I',*values))==rectangle
                                    assert uc.reg_read(UC_ARM_REG_R0)==child and uc.reg_read(UC_ARM_REG_R1)==0x333c8afb+slide and struct.unpack('<I',uc.mem_read(sp+8,4))[0]==0
                                    observed.append('original UIImageView setFrame -> modeled native geometry')
                                else:return
                                # Legal caller-clobbered state at an Objective-C boundary.
                                for r in rs[:4]+[UC_ARM_REG_R12]:uc.reg_write(r,0x12345678)
                                uc.reg_write(UC_ARM_REG_CPSR,uc.reg_read(UC_ARM_REG_CPSR)^(1<<29));uc.reg_write(UC_ARM_REG_FPSCR,0x07000000)
                                for i in list(range(8))+list(range(16,32)):uc.reg_write(ds[i],0x50000000+i)
                                uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
                            u.hook_add(UC_HOOK_CODE,boundary);u.emu_start(site|1,site+12,count=1000)
                            state=dict(regs=[u.reg_read(r) for r in rs],fp=[u.reg_read(r) for r in ds],fpscr=u.reg_read(UC_ARM_REG_FPSCR),stack=bytes(u.mem_read(0x20010000,128)).hex())
                            comparisons.append(state);calls=observed
                            mask=struct.unpack('<I',u.mem_read(0x15001028,4))[0]
                            assert mask==((0x89abcdef&~(63<<15))|(18<<15)) if enabled and child and parent else mask==0x89abcdef
                        assert comparisons[0]==comparisons[1],'Caller state or original PC-relative continuation changed'
                        assert len(calls)==(2 if child and parent else 0)
                        results.append(dict(slide=hex(slide),header=hex(header),rectangle=rectangle,child=hex(child),parent=hex(parent),native_calls=calls,caller_state_equal=True))
    result=dict(result='PASS',independent_assembler=True,production_bytecode=True,original_UIView_bounds_and_UIImageView_frame_and_autoresizing=True,cases=results,
        limits='CALayer bounds/geometry Objective-C boundaries are modeled; UIKit screenshot/background coverage requires hardware')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS independent encoding and 12 Cortex-A9 original API/CGRect/ASLR/nil/caller GPR/APSR/FPSCR/VFP/stack cases',flush=True)
if __name__=='__main__':main()
