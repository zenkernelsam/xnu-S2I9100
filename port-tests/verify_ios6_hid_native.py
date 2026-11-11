#!/usr/bin/env python3
"""Run the original iOS6 Thumb HID decoder/getters on emitted driver bytes.

CF data/array allocation is modeled; this checks the native wire ABI, not UI
delivery or a complete iOS process. All execution belongs in the CPU envelope.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'assets/py-native-hid'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR, UC_ARM_REG_FPEXC

REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
STACK, HEAP, STOP = 0x20000000, 0x21000000, 0x2001ffc0
STUBS = {0x31d48498:'data_ptr', 0x31d484b8:'data_len',
         0x31d48f58:'bcopy', 0x31d49348:'memset', 0x31d48d28:'new_instance',
         0x31d495b8:'register_class', 0x31d482d8:'new_array',
         0x31d48158:'append_array', 0x31d482f8:'array_count',
         0x31d48338:'array_value', 0x31d487d8:'release', 0x31d487e8:'retain'}


class Native:
    def __init__(self, cache, data, cpu_model=None):
        self.cache, self.next, self.arrays, self.logs = cache, HEAP+0x10000, {}, []
        mo, mc = struct.unpack_from('<2I', cache, 16)
        self.maps = [struct.unpack_from('<QQQII', cache, mo+i*32) for i in range(mc)]
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        if cpu_model is not None:
            # CPU selection must precede memory access/JIT initialization.
            self.uc.ctl_set_cpu_model(cpu_model)
            self.uc.cpr_write(15,0,1,0,2,0,False,0xf00000)
            self.uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
        self.uc.mem_map(STACK, 0x20000)
        self.uc.mem_map(HEAP, 0x200000)
        self.uc.mem_map(0x31d48000, 0x2000)
        for address in STUBS:
            self.uc.mem_write(address, struct.pack('<I', 0xe12fff1e))  # ARM bx lr
        self.uc.mem_write(HEAP, struct.pack('<2I', HEAP+0x1000, len(data)))
        self.uc.mem_write(HEAP+0x1000, data)
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self.page)
        self.uc.hook_add(UC_HOOK_CODE, self.code)

    def page(self, uc, access, address, size, value, user):
        page = address & ~4095
        for start, length, offset, _, _ in self.maps:
            if start <= page and page+4096 <= start+length:
                uc.mem_map(page, 4096)
                at = offset+page-start
                uc.mem_write(page, self.cache[at:at+4096])
                return True
        return False

    def alloc(self, size):
        value = self.next
        self.next += (size+15) & ~15
        assert self.next < HEAP+0x200000
        return value

    def code(self, uc, address, size, user):
        if address == STOP:
            uc.emu_stop()
            return
        if address == 0x31d1fbb8:
            self.logs.append('native IOHID size diagnostic')
            self.return_stub(uc)
            return
        if address not in STUBS:
            return
        r = [uc.reg_read(reg) for reg in REGS]
        name, result = STUBS[address], 0
        if name == 'data_ptr': result = self.u32(r[0])
        elif name == 'data_len': result = self.u32(r[0]+4)
        elif name == 'bcopy':
            assert r[2] < 4096, (name, [hex(x) for x in r])
            uc.mem_write(r[1], bytes(uc.mem_read(r[0], r[2])))
        elif name == 'memset':
            assert r[2] < 4096, (name, [hex(x) for x in r])
            uc.mem_write(r[0], bytes([r[1] & 255])*r[2])
            result = r[0]
        elif name == 'new_instance': result = self.alloc(r[2]+8)
        elif name == 'register_class': result = 1
        elif name == 'new_array':
            result = self.alloc(16)
            self.arrays[result] = []
        elif name == 'append_array': self.arrays[r[0]].append(r[1])
        elif name == 'array_count': result = len(self.arrays[r[0]])
        elif name == 'array_value': result = self.arrays[r[0]][r[1]]
        elif name == 'retain': result = r[0]
        uc.reg_write(UC_ARM_REG_R0, result)
        self.return_stub(uc)

    def return_stub(self, uc):
        # The native image reaches CF veneers both with blx and a Thumb
        # tail branch. Return in the caller's state instead of imposing ARM.
        lr = uc.reg_read(UC_ARM_REG_LR)
        cpsr = uc.reg_read(UC_ARM_REG_CPSR)
        uc.reg_write(UC_ARM_REG_CPSR, (cpsr & ~32) | (32 if lr & 1 else 0))
        uc.reg_write(UC_ARM_REG_PC, lr)

    def u32(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def call(self, address, *args):
        for reg, value in zip(REGS, list(args)+[0]*4):
            self.uc.reg_write(reg, value)
        self.uc.reg_write(UC_ARM_REG_SP, STACK+0x10000)
        self.uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        self.uc.emu_start(address | 1, STOP, count=50000)
        return self.uc.reg_read(UC_ARM_REG_R0)

    def decode(self):
        event = self.call(0x31d2329c, 0, HEAP)
        result = dict(event=hex(event), diagnostics=self.logs)
        if not event:
            return result
        result.update(type=self.call(0x31d220a8, event), size=self.u32(event+0x20))
        if result['type'] == 3:
            result['keyboard'] = [self.call(0x31d22250, event, (3<<16)+n) for n in range(3)]
        children = self.arrays.get(self.u32(event+0x18), [])
        result['children'] = [dict(type=self.call(0x31d220a8, child),
            size=self.u32(child+0x20), depth=self.uc.mem_read(child+0x34,1)[0],
            x=self.u32(child+0x40), y=self.u32(child+0x44),
            transducer_type=self.u32(child+0x50)) for child in children]
        if result['type']==11:result['transducer_type']=self.u32(event+0x50)
        return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prefix', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    assert a.output.resolve().is_relative_to(ROOT / 'neighbor/xnu/out')
    with (ROOT / 'assets/ios6-extract/dyld_cache6_armv7').open('rb') as f:
        cache = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        results = {}
        for name in ('home6','touch6','home7'):
            data = Path(str(a.prefix)+'-'+name+'.bin').read_bytes()
            print('NATIVE', name, flush=True)
            result = Native(cache, data).decode()
            result['wire_sha256'] = hashlib.sha256(data).hexdigest()
            results[name] = result
        assert results['home6']['type'] == 3 and results['home6']['size'] == 40
        assert results['home6']['keyboard'] == [12, 64, 1]
        assert results['home7'].get('keyboard') != [12, 64, 1]
        assert results['touch6']['type'] == 11 and results['touch6']['size'] == 104
        child, = results['touch6']['children']
        # Native factory instruction at31d21d36 is movs r3,#0x22;
        # the generic factory stores r3 as the transducer type at+50h.
        def fileoff(va):
            mo,mc=struct.unpack_from('<2I',cache,16)
            maps=[struct.unpack_from('<QQQII',cache,mo+n*32) for n in range(mc)]
            for start,size,offset,_,_ in maps:
                if start<=va<start+size:return offset+va-start
            raise ValueError(hex(va))
        assert cache[fileoff(0x31d21d36):fileoff(0x31d21d36)+2]==b'\x22\x23'
        assert results['touch6']['transducer_type']==0x23
        assert child == dict(type=11, size=104, depth=1, x=240*65536//479, y=400*65536//799,transducer_type=0x22)
        report = dict(result='PASS original iOS6 HID instructions', cf_runtime='modeled',
                      decoder='0x31d2329c', getters=['0x31d220a8','0x31d22250'], results=results)
        a.output.write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
