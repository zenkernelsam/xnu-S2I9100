#!/usr/bin/env python3
"""Check the supplied iOS6 slab/assert prologues and production capture guard.

MMU, abort's outer frames and imported canary/__assert resolution are modeled.
No allocator or termination behavior is replaced in the kernel.
"""
import os
from pathlib import Path
import sys

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice',
                          '-c2', '-n7', sys.executable, '-B', __file__],
              dict(os.environ, I9100_CPU_CAPPED='1'))

import hashlib
import json
import mmap
import struct
import subprocess
from datetime import datetime

REPO = Path(__file__).resolve().parents[1]
PROJECT = REPO.parents[1]
sys.path.insert(0, str(PROJECT / 'assets/py-native-hid'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_R1,
                              UC_ARM_REG_R2, UC_ARM_REG_R7, UC_ARM_REG_SP,
                              UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPEXC)

out = REPO / 'out' / ('ios6-slab-capture-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
cases = []
with (PROJECT / 'assets/ios6-extract/dyld_cache6_armv7').open('rb') as stream:
    cache = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
    digest = hashlib.sha256(cache).hexdigest()
    if digest != '51a73fb0d39f2d7689641a1572f7895105902f5dd1792c2fc9d96eb6d74506dc':
        raise ValueError('Expected the verified supplied iOS6 shared cache')
    offset, count = struct.unpack_from('<2I', cache, 16)
    mappings = [struct.unpack_from('<3Q2I', cache, offset+32*n) for n in range(count)]

    def page(cpu, access, address, size, value, unused):
        base = address & ~4095
        for va, length, file_offset, _, _ in mappings:
            if va <= base and base+4096 <= va+length:
                cpu.mem_map(base, 4096)
                start = file_offset+base-va
                cpu.mem_write(base, cache[start:start+4096])
                return True
        return False

    for alignment in (0, 8):
        for bad in (False, True):
            cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
            cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
            cpu.mem_map(0x10000000, 0x10000)
            cpu.cpr_write(15, 0, 1, 0, 2, 0, False, 0xf00000)
            cpu.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
            context, candidate, bucket = 0x10000000, 0x10003000, 7
            link = context+bucket*32+0x60
            expected = 0xdeafbe00 | bucket
            cpu.mem_write(link, struct.pack('<I', candidate))
            cpu.mem_write(candidate, struct.pack('<IIIHHI', link-4, link-4,
                          candidate+0x100, 10, 0, 0x12345678 if bad else expected))
            cpu.mem_write(candidate+0x100, bytes(4))
            cpu.mem_write(0x1000a000, struct.pack('<I', 0xcafe1234))
            observed = {}

            def hook(cpu, address, size, unused):
                if address == 0x32ce2384:
                    # Model the imported stack-canary pointer only.
                    cpu.reg_write(UC_ARM_REG_R2, 0x1000a000)
                    cpu.reg_write(UC_ARM_REG_PC, address+3)
                elif address == 0x32ce246a:
                    observed['local'] = cpu.reg_read(UC_ARM_REG_SP)
                elif address == 0x32dd6ad4:
                    # Resolve the real __assert_rtn import without libc I/O.
                    observed['asserted'] = True
                    cpu.reg_write(UC_ARM_REG_PC, 0x3926141d)
                elif address == 0x39261426:
                    assert_fp = cpu.reg_read(UC_ARM_REG_R7)
                    allocator_fp, allocator_lr = struct.unpack('<2I', cpu.mem_read(assert_fp, 8))
                    parent_fp, parent_lr = struct.unpack('<2I', cpu.mem_read(allocator_fp, 8))
                    slots = struct.unpack('<2I', cpu.mem_read(observed['local']+36, 8))
                    assert allocator_lr == 0x32ce24f7 and parent_lr == 0x32ce2339
                    assert slots == (link, expected)
                    assert struct.unpack('<I', cpu.mem_read(candidate+16, 4))[0] == 0x12345678
                    observed.update(allocator_fp=allocator_fp, sp=cpu.reg_read(UC_ARM_REG_SP),
                                    frame2=allocator_lr, frame3=parent_lr)
                    cpu.emu_stop()
                elif address == 0x32ce2338:
                    observed['returned'] = True
                    cpu.emu_stop()

            cpu.hook_add(UC_HOOK_MEM_UNMAPPED, page)
            cpu.hook_add(UC_HOOK_CODE, hook)
            cpu.reg_write(UC_ARM_REG_R0, context)
            cpu.reg_write(UC_ARM_REG_R1, bucket)
            cpu.reg_write(UC_ARM_REG_SP, 0x1000f000+alignment)
            cpu.reg_write(UC_ARM_REG_LR, 0x32ce2339)
            cpu.emu_start(0x32ce2351, 0, count=10000)
            assert bool(observed.get('asserted')) == bad
            if not bad:
                assert observed.get('returned') and cpu.reg_read(UC_ARM_REG_R0) == candidate+0x100
            cases.append(dict(alignment=alignment, bad=bad, **observed))

(out / 'native-cases.json').write_text(json.dumps(dict(cache_sha256=digest, cases=cases), indent=2)+'\n')
source = ['#include <assert.h>', '#include <stdio.h>', '#include <string.h>', '#include <stddef.h>',
          '#include "' + str(REPO / 'pexpert/arm/i9100_abort_observation.h') + '"',
          'int main(void) { struct I9100AbortTrace t; unsigned checks=0;',
          'assert(sizeof(struct I9100AbortObservation)==368);',
          'assert(offsetof(struct I9100AbortObservation,text)==124);']
for case in cases:
    if not case['bad']:
        continue
    source += ['memset(&t,0,sizeof(t)); t.count=4;',
               f't.sp={case["sp"]}U; t.frames[0]=0x39280973U; t.frames[1]=0x392614d1U;',
               f't.frames[2]={case["frame2"]}U; t.frames[3]={case["frame3"]}U;',
               f'unsigned fp{case["alignment"]}={case["allocator_fp"]}U;',
               f'assert(i9100_ios6_slab_local(&t,fp{case["alignment"]})=={case["local"]}U); checks++;']
source += ['struct I9100AbortTrace valid=t;',
           'for(unsigned n=0;n<4;n++){t=valid;t.frames[n]^=2;assert(!i9100_ios6_slab_local(&t,0x1000eff8));checks++;}',
           't=valid;t.count=3;assert(!i9100_ios6_slab_local(&t,0x1000eff8));checks++;',
           't=valid;t.sp=0;assert(!i9100_ios6_slab_local(&t,0x1000eff8));checks++;',
           't=valid;t.sp=0x40000000U;assert(!i9100_ios6_slab_local(&t,0x1000eff8));checks++;',
           'unsigned invalid[]={0,159,0x3ffffff4U,0x1000eff9U};',
           'for(unsigned n=0;n<4;n++){assert(!i9100_ios6_slab_local(&valid,invalid[n]));checks++;}',
           'assert(!i9100_ios6_slab_local(NULL,0x1000eff8));checks++;',
           'printf("%u production frame/stack scope checks passed\\n",checks);return 0;}']
(out / 'guard.c').write_text('\n'.join(source)+'\n')
subprocess.run(['cc', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                str(out / 'guard.c'), '-o', str(out / 'guard')], check=True)
subprocess.run([str(out / 'guard')], check=True)
print('4 actual iOS6 ARM slab/assert prologue cases passed; MMU/abort/imports modeled')
print('EVIDENCE', out)
