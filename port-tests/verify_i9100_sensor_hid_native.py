#!/usr/bin/env python3
"""Run original iOS6 factories/getters/CoreMotion callbacks on actual driver bytes."""
import argparse
import hashlib
import importlib.util
import json
import mmap
import os
from pathlib import Path
import struct
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
REPO=Path(__file__).resolve().parents[1];ROOT=REPO.parent
sys.path.insert(0,str(ROOT/'assets/py-native-hid'))
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_CPU_ARM_CORTEX_A9,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_CPSR,UC_ARM_REG_PC


def imported(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--directory',type=Path,required=True);a=p.parse_args()
    directory=a.directory.resolve()
    if not directory.is_relative_to(REPO/'out'):p.error('Read and retain evidence inside XNU out')
    manifest=json.loads((directory/'manifest.json').read_text())
    if manifest.get('result')!='PASS' or not manifest.get('emitted_packets'):raise RuntimeError('Require current successful emitted-packet proof')
    old=imported('motion_native_decoder',REPO/'port-tests/verify_ios6_hid_native.py')
    symbols=imported('motion_native_cache',ROOT/'uniloader-S2I9100/scripts/i9100/symbolize-ios6-stacks.py')
    source=ROOT/'assets/ios6-extract/dyld_cache6_armv7'
    results={};code_proofs={}
    with source.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
        cache=symbols.Cache(data)
        base=next(base for base,name in cache.images if '/IOKit.framework/' in name)
        syms={name:va for va,name in cache.image_symbols(base)[1]}
        timestamp=syms['_IOHIDEventGetTimeStamp']
        for kind,etype,factory,callback in ((1,13,0x31d219ac,0x316eb2a8),(2,20,0x31d219d4,0x316eb408),(3,21,0x31d219fc,0x316eb520)):
            packet=(directory/f'sensor-{kind}.bin').read_bytes();meta=json.loads((directory/f'sensor-{kind}.json').read_text())
            assert hashlib.sha256(packet).hexdigest()==manifest['emitted_packets'][f'sensor-{kind}.bin']
            assert len(packet)==60 and struct.unpack_from('<II',packet,16)==(44,etype)
            native=old.Native(data,packet,UC_CPU_ARM_CORTEX_A9)
            event=native.call(0x31d2329c,0,old.HEAP)
            assert event and native.call(0x31d220a8,event)==etype and native.u32(event+0x20)==44
            fixed=struct.unpack_from('<3i',packet,48);expected=[value/65536.0 for value in fixed]
            actual=[struct.unpack('<f',struct.pack('<I',native.call(0x31d22530,event,(etype<<16)+axis)))[0] for axis in range(3)]
            assert actual==expected,(kind,actual,expected)
            # The original factory takes allocator,r1/r2 timestamp,float x in
            # r3 and float y/z/options on the stack (Darwin ARM32 alignment).
            native.uc.mem_write(old.STACK+0x10000,struct.pack('<ffI',expected[1],expected[2],0))
            generated=native.call(factory,0,meta['ticks']&0xffffffff,meta['ticks']>>32,
                                  struct.unpack('<I',struct.pack('<f',expected[0]))[0])
            assert generated and native.u32(generated+0x20)==44
            assert bytes(native.uc.mem_read(generated+0x40,12))==packet[48:60]
            captured=[];context=old.HEAP+0x800;receiver=old.STOP-0x100
            native.uc.mem_write(context,bytes(32));slot={1:4,2:12,3:20}[kind]
            native.uc.mem_write(context+slot,struct.pack('<II',receiver|1,0x1234))
            targets={0x317287e4:0x31d220a8,0x317287b4:0x31d22530,0x317287c4:0x31d22250,0x317287d4:timestamp}
            def hook(uc,address,size,user):
                if address in targets:
                    uc.reg_write(UC_ARM_REG_CPSR,uc.reg_read(UC_ARM_REG_CPSR)|32)
                    uc.reg_write(UC_ARM_REG_PC,targets[address]|1);return
                if address==0x316c2c78:
                    ticks=uc.reg_read(UC_ARM_REG_R0)|(uc.reg_read(UC_ARM_REG_R1)<<32)
                    words=struct.unpack('<II',struct.pack('<d',ticks*3.0/1e9))
                    uc.reg_write(UC_ARM_REG_R0,words[0]);uc.reg_write(UC_ARM_REG_R1,words[1]);native.return_stub(uc);return
                if address==receiver:
                    assert uc.reg_read(UC_ARM_REG_R0)==0x1234
                    xyz=struct.unpack('<3f',uc.mem_read(uc.reg_read(UC_ARM_REG_R1),12))
                    when=struct.unpack('<d',uc.mem_read(uc.reg_read(UC_ARM_REG_R2),8))[0]
                    captured.append(dict(xyz=list(xyz),seconds=when,subtype=uc.reg_read(UC_ARM_REG_R3) if kind==2 else None))
                    native.return_stub(uc)
            native.uc.hook_add(UC_HOOK_CODE,hook)
            native.call(callback,context,0,0,event)
            assert len(captured)==1 and captured[0]['xyz']==expected,(kind,captured,expected)
            assert abs(captured[0]['seconds']-meta['ticks']*3.0/1e9)<1e-12
            results[str(kind)]=dict(packet_sha256=hashlib.sha256(packet).hexdigest(),type=etype,size=44,
                fixed=list(fixed),float=actual,callback=captured[0],factory_data_matched=True)
            for address,length in ((factory,40),(callback,128),(0x31d2329c,768),(0x31d22530,0x330),(timestamp,32)):
                at=cache.offset(address,length);code_proofs[f'{address:08x}']=hashlib.sha256(data[at:at+length]).hexdigest()
    report=dict(result='PASS',hardware_tested=False,original_cache=str(source.relative_to(ROOT)),
        cpu='Cortex-A9',cf_runtime='modeled',absolute_time_conversion='modeled native tick numerator3/denominator1',
        original_factories_getters_callbacks=True,service_discovery_verified=False,results=results,code_sha256=code_proofs,
        verifier_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),model_manifest_sha256=hashlib.sha256((directory/'manifest.json').read_bytes()).hexdigest())
    (directory/'native-motion.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS actual controller/worker/collector packets through original iOS6 decoder, float getters, vector factories and three CoreMotion callbacks; CF/timebase modeled, no service/physical proof')


if __name__=='__main__':main()
