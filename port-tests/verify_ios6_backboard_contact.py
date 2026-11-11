#!/usr/bin/env python3
"""Execute supplied BackBoard digitizer routing on actual queue bytes.

Original Mach-O instructions and IOHID getters execute. CF containers,
Foundation, window coordinate helpers and final event delivery are modeled.
This is a routing-contract check, not physical UI delivery validation.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
import argparse,json,mmap,runpy,struct,math
from pathlib import Path
root=Path(__file__).resolve().parents[3]
m=runpy.run_path(str(Path(__file__).with_name('verify_ios6_hid_native.py')))
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import *
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--wire',type=Path,required=True)
p.add_argument('--sequence-prefix',type=Path,help='Use produced down/move/up files in one original routing instance')
p.add_argument('--globals',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
asset=(root/'assets/ios6-extract/backboardd.bin/backboardd').read_bytes()
pos=28;segments=[];sections=[];sym=dy=None
for _ in range(struct.unpack_from('<I',asset,16)[0]):
    cmd,size=struct.unpack_from('<2I',asset,pos)
    if cmd==1:
        segments.append(struct.unpack_from('<4I',asset,pos+24))
        for n in range(struct.unpack_from('<I',asset,pos+48)[0]):
            sections.append(struct.unpack_from('<9I',asset,pos+56+n*68+32))
    if cmd==2:sym=struct.unpack_from('<4I',asset,pos+8)
    if cmd==11:dy=struct.unpack_from('<18I',asset,pos+8)
    pos+=size
imports={}
for at,size,_,_,_,_,flags,r1,r2 in sections:
    if flags&255!=8:continue
    for off in range(0,size,r2):
        ni=struct.unpack_from('<I',asset,dy[12]+(r1+off//r2)*4)[0]
        so,_,st,_=sym;si=struct.unpack_from('<I',asset,so+12*ni)[0]
        imports[at+off]=asset[st+si:asset.find(b'\0',st+si)].decode()
def bits(f):return struct.unpack('<I',struct.pack('<f',f))[0]
def float32(v):return struct.unpack('<f',struct.pack('<I',v))[0]
result=dict(contract='original BackBoard/IOHID instructions; CF/Foundation/window/dispatch boundaries modeled',events=[],calls=[])
with (root/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as file:
    cache=mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ)
    native=m['Native'](cache,a.wire.read_bytes(),UC_CPU_ARM_CORTEX_A9)
    decoded=native.decode();event=int(decoded['event'],16)
    for vm,vs,fo,fs in segments:
        if not fs:continue
        native.uc.mem_map(vm,((vs+4095)//4096)*4096)
        native.uc.mem_write(vm,asset[fo:fo+fs])
    native.uc.mem_write(0x46000,a.globals.read_bytes())
    # The standalone original Mach-O has an unbound __stack_chk_guard GOT
    # slot. Model that runtime import, without changing instructions.
    native.uc.mem_write(0x3c58c,struct.pack('<I',0x2100f800))
    native.uc.mem_write(0x2100f800,struct.pack('<I',0xa5b6c7d8))
    # Native CGPointZero import at3c618 supplies two floats, not an HID
    # state value. The standalone image leaves this GOT slot unbound.
    native.uc.mem_write(0x3c618,struct.pack('<I',0x2100f810))
    native.uc.mem_write(0x2100f810,bytes(8))
    getters={'_IOHIDEventGetType':0x31d220a8,'_IOHIDEventGetIntegerValue':0x31d22250,
             '_IOHIDEventGetFloatValue':0x31d22530,'_IOHIDEventGetTimeStamp':0x31d220e0}
    def hook(uc,address,size,unused):
        regs=[uc.reg_read(x) for x in m['REGS']]
        if address in (0xb60c,0x6960,0x6ac0,0x6d78,0xdcc4,0xddd4,0xb818):
            value=0
            if address==0xb60c:value=a.globals.read_bytes()[0x4e0]&1
            elif address==0x6d78:value=1
            elif address==0xdcc4:
                uc.mem_write(regs[0],struct.pack('<2I',regs[1],regs[2]))
                if regs[3]:uc.mem_write(regs[3],struct.pack('<I',1))
            elif address==0xddd4:value=1
            elif address==0xb818:
                data=bytes(uc.mem_read(regs[0],0x74));result['events'].append(dict(
                    header=[hex(v) for v in struct.unpack_from('<22I',data)],
                    contacts=data[0x56],contact_hex=data[0x58:].hex(),
                    record_address=hex(regs[0]),window=regs[1],mode=regs[2]))
                (a.output/f'event-{len(result["events"])}.bin').write_bytes(data)
            uc.reg_write(UC_ARM_REG_R0,value);native.return_stub(uc);return
        if address not in imports:return
        name=imports[address];result['calls'].append(name)
        if name in getters:
            uc.reg_write(UC_ARM_REG_CPSR,uc.reg_read(UC_ARM_REG_CPSR)|32)
            uc.reg_write(UC_ARM_REG_PC,getters[name]|1);return
        value=0
        if name=='_IOHIDEventGetChildren':value=native.u32(regs[0]+0x18)
        elif name=='_CFArrayGetCount':value=len(native.arrays[regs[0]])
        elif name=='_CFArrayGetValueAtIndex':value=native.arrays[regs[0]][regs[1]]
        elif name=='_roundf':
            v=float32(regs[0]);value=bits(math.floor(v+0.5) if v>=0 else math.ceil(v-0.5))
        elif name=='_objc_msgSend':value=0x2100f000
        elif name=='_memset':
            assert regs[2]<8192;uc.mem_write(regs[0],bytes([regs[1]&255])*regs[2]);value=regs[0]
        elif name=='___memcpy_chk':
            assert regs[2]<=regs[3] and regs[2]<8192
            uc.mem_write(regs[0],bytes(uc.mem_read(regs[1],regs[2])));value=regs[0]
        elif name in ('___assert_rtn','___stack_chk_fail'):
            result['failure']=name;uc.emu_stop();return
        else:result.setdefault('modeled_imports',[]).append(name)
        uc.reg_write(UC_ARM_REG_R0,value);native.return_stub(uc)
    native.uc.hook_add(UC_HOOK_CODE,hook)
    try:
        if a.sequence_prefix:
            for name in ('down','move','up'):
                data=Path(str(a.sequence_prefix)+'-'+name+'.bin').read_bytes()
                native.uc.mem_write(m['HEAP']+0x1000,data)
                current=native.decode();result.setdefault('sequence',[]).append(dict(phase=name,decoded=current))
                native.call(0xcf08,int(current['event'],16))
        else:native.call(0xcf08,event)
    except Exception as error:
        result['execution_error']=str(error)
        result['pc']=hex(native.uc.reg_read(UC_ARM_REG_PC))
        result['registers']=[hex(native.uc.reg_read(reg)) for reg in m['REGS']]
    result['decoded']=decoded
    result['calls']=dict((x,result['calls'].count(x)) for x in set(result['calls']))
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    if result.get('execution_error') or result.get('failure'):
        raise SystemExit(1)
    if a.sequence_prefix:
        assert len(result['events'])==3,'The original route must produce down/move/up'
        assert [int(r['header'][13],16) for r in result['events']]==[1,2,6]
        xy=[struct.unpack('<2f',struct.pack('<2I',int(r['header'][2],16),int(r['header'][3],16))) for r in result['events']]
        assert xy==[(130.0,751.0),(251.0,751.0),(381.0,751.0)],xy
