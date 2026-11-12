#!/usr/bin/env python3
"""Run the original iOS6 LS callback/demux/stub to classify absent replies.

No phone access. Original ARM instructions execute; CF/ObjC, vm allocation,
business lookup, IPC and delivery are modeled. This establishes branches,
not which branch a real missing reply followed or a repaired LS service.
"""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice',
                         '-c2', '-n7', sys.executable, '-B', __file__, *sys.argv[1:]],
               dict(os.environ, I9100_CPU_CAPPED='1'))

import argparse
from datetime import datetime
import hashlib
import json
import mmap
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'assets/py-native-hid'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_R1,
                              UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4,
                              UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

HEAP, STACK, STOP = 0x10000000, 0x10010000, 0x1001fff0
REQ, REPLY, TOKEN, TARGET = HEAP, HEAP + 0x2000, HEAP + 0x4000, HEAP + 0x4010
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)


def run(cache, request, case):
    mo, mc = struct.unpack_from('<2I', cache, 16)
    maps = [struct.unpack_from('<QQQII', cache, mo + n * 32) for n in range(mc)]
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    cpu.mem_map(HEAP, 0x20000)
    mapped_pages = set()
    record = dict(case=case['name'], calls=[], sends=[], reached=[])
    recent = []
    header = bytearray(request)
    struct.pack_into('<6I', header, 0, 0x1112, case.get('header_size', 288), case.get('remote', 0x5555), 0x1203, 0, case.get('id', 14012))
    # The received audit trailer is modeled from the documented ARM32 wire
    # contract; the full native 14012 receive was not saved on the phone.
    header.extend(struct.pack('<13I', 0, 52, 0, 0, 501, 0, 501, 501, 0, 38, 0, 0, 0))
    cpu.mem_write(REQ, bytes(header)); cpu.mem_write(TARGET, struct.pack('<I', 0x303))
    def page(uc, access, address, size, value, _):
        start = address & ~4095
        for va, length, offset, _, _ in maps:
            if va <= start and start + 4096 <= va + length:
                if start in mapped_pages:return True
                uc.mem_map(start, 4096); at = offset + start - va
                uc.mem_write(start, cache[at:at + 4096]); mapped_pages.add(start);return True
        return False
    def ret(value=0):
        cpu.reg_write(UC_ARM_REG_R0, value & 0xffffffff)
        if cpu.reg_read(UC_ARM_REG_PC) in trampolines:
            # Allow the modeled import's actual ARM bx lr to leave the
            # caller's Thumb IT block. A direct PC write inside that block
            # is not equivalent in Unicorn's translation state.
            return
        lr = cpu.reg_read(UC_ARM_REG_LR)
        cpsr = cpu.reg_read(UC_ARM_REG_CPSR)
        cpu.reg_write(UC_ARM_REG_CPSR, (cpsr & ~32) | (32 if lr & 1 else 0))
        cpu.reg_write(UC_ARM_REG_PC, lr)
    def u32(address): return struct.unpack('<I', cpu.mem_read(address, 4))[0]
    def hook(uc, address, size, _):
        r = [uc.reg_read(reg) for reg in REGS]
        recent.append(dict(pc=hex(address), lr=hex(uc.reg_read(UC_ARM_REG_LR)),
                           cpsr=hex(uc.reg_read(UC_ARM_REG_CPSR)), r0=hex(r[0])))
        if len(recent)>40:recent.pop(0)
        if address in trampolines.values():return
        address=trampolines.get(address,address)
        if address == STOP:
            record['returned'] = True; uc.emu_stop()
        elif address == 0x32492dc0:
            # Autorelease-pool Objective-C boundary only.
            record['calls'].append('objc autorelease boundary'); ret(0x1234)
        elif address == 0x32471b0a:
            # Bind the original vm_allocate import's mach_task_self_ pointer.
            uc.reg_write(UC_ARM_REG_R6, TARGET)
            uc.reg_write(UC_ARM_REG_PC, address + 3)
        elif address == 0x32492aa0:
            record['calls'].append('vm_allocate'); record['allocation'] = dict(size=r[2], flags=r[3])
            if not case.get('allocation_failure'):
                uc.mem_write(r[1], struct.pack('<I', REPLY)); ret(0)
            else: ret(3)
        elif address == 0x324643d8:
            raw = bytes(uc.mem_read(r[1], 256))
            record['lookup_name'] = raw.split(b'\0', 1)[0].decode('ascii')
            record['reached'].append('native 14012 stub accepted')
            if case.get('native_business'):
                # Execute the original 324643d8 implementation as well.
                # Only its imports and nested CF/authorization/lookup helpers
                # are modeled below; its stores and final return are original.
                record['business_outer'] = 'original instructions'
            else:
                record['business_outer'] = 'modeled return supplied by test'
                uc.mem_write(r[2], struct.pack('<I', case.get('boolean', 0)))
                ret(case.get('business_result', 0))
        elif case.get('native_business') and address in (0x32492d60, 0x32492d70):
            record['calls'].append('modeled xpc transaction import')
            # Exercise nonzero import return values. The original outer
            # function overwrites the final r0 after transaction_end.
            ret(case.get('dependency_result', 0))
        elif case.get('native_business') and address == 0x32464404:
            # Bind CFString's allocator GOT before the original two loads.
            uc.mem_write(HEAP + 0x4030, struct.pack('<I', HEAP + 0x4040))
            uc.mem_write(HEAP + 0x4040, bytes(4))
            uc.reg_write(UC_ARM_REG_R0, HEAP + 0x4030)
        elif case.get('native_business') and address == 0x324938a0:
            record['calls'].append('modeled CFStringCreateWithCString')
            ret(0 if case.get('cf_string_failure') else HEAP + 0x5000)
        elif case.get('native_business') and address == 0x3245b9a8:
            record['calls'].append('modeled LS authorization context helper')
            ret(case.get('dependency_result', 0))
        elif case.get('native_business') and address == 0x324526a4:
            record['calls'].append('modeled nested LS lookup')
            record['lookup_argument'] = hex(r[0])
            ret(case.get('lookup_result', 0))
        elif case.get('native_business') and address == 0x32493600:
            record['calls'].append('modeled CFRelease'); ret(case.get('dependency_result', 0))
        elif case.get('native_business') and address == 0x3246442c:
            record['reached'].append('native unconditional RetCode=0 return')
        elif address == 0x324926d0:
            # libc memchr boundary, used by the original generated stub.
            raw = bytes(uc.mem_read(r[0], r[2])); pos = raw.find(bytes([r[1] & 255]))
            ret(r[0] + pos if pos >= 0 else 0)
        elif address in (0x32469088, 0x3246832c, 0x3246834e, 0x32468374, 0x324683a2, 0x324683b0):
            # Original NDR GOT binding; replace no instruction or response.
            uc.mem_write(TOKEN, struct.pack('<II', 0, 1))
            uc.mem_write(HEAP + 0x4020, struct.pack('<I', TOKEN))
            uc.reg_write(UC_ARM_REG_R1 if address == 0x324683a2 else UC_ARM_REG_R0, HEAP + 0x4020)
        elif address == 0x32492630:
            raw = bytes(uc.mem_read(r[0], r[2])); words = struct.unpack_from('<6I', raw)
            record['sends'].append(dict(options=r[1], size=r[2], bits=hex(words[0]),
                                        remote=hex(words[2]), id=words[5], retcode=u32(r[0] + 32),
                                        output_boolean=u32(r[0] + 36) if r[2] >= 40 else None))
            ret(case.get('send_result', 0))
        elif address == 0x32492640:
            record['calls'].append('mach_msg_destroy'); ret()
        elif address == 0x32492ac0:
            record['calls'].append('vm_deallocate'); ret()
    # Imports are intercepted at their cache stubs; no message is delivered.
    from unicorn.arm_const import UC_ARM_REG_R6
    trampolines={}
    if case.get('native_business'):
        def cache_read(va,n):
            for base,length,offset,_,_ in maps:
                if base<=va and va+n<=base+length:
                    return cache[offset+va-base:offset+va-base+n]
            raise ValueError('Import address outside original cache')
        for n,stub in enumerate((0x32492d60,0x32492d70,0x324938a0,0x32493600)):
            # Bind original ARM lazy-import veneers through their actual
            # GOT words, without replacing veneer or caller instructions.
            literal=struct.unpack('<i',cache_read(stub+12,4))[0]
            got=stub+12+literal
            page(cpu,0,got,4,0,None)
            trampoline=HEAP+0x8000+n*16
            cpu.mem_write(got,struct.pack('<I',trampoline))
            cpu.mem_write(trampoline,struct.pack('<I',0xe12fff1e))
            trampolines[trampoline]=stub
    cpu.hook_add(UC_HOOK_MEM_UNMAPPED, page); cpu.hook_add(UC_HOOK_CODE, hook)
    for reg, value in zip(REGS, (0, REQ, 0, 0)): cpu.reg_write(reg, value)
    cpu.reg_write(UC_ARM_REG_SP, STACK + 0xf000); cpu.reg_write(UC_ARM_REG_LR, STOP | 1)
    try:
        cpu.emu_start(0x32471aa5, STOP, count=20000)
    except BaseException:
        print('MODEL STOP', case['name'], 'PC', hex(cpu.reg_read(UC_ARM_REG_PC)),
              'registers', [hex(cpu.reg_read(reg)) for reg in REGS], record, flush=True)
        print('MODEL TRACE', json.dumps(recent), flush=True)
        raise
    record['returned'] = record.get('returned', False) or cpu.reg_read(UC_ARM_REG_PC) == STOP
    if not record['returned']: raise ValueError('Original callback did not return within instruction budget')
    return record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--request', type=Path, required=True)
    a = p.parse_args(); source = a.request.resolve()
    if not source.is_relative_to(ROOT / 'neighbor/uniloader/out'): p.error('Use saved real request from loader out')
    request = source.read_bytes()
    if len(request) != 288 or struct.unpack_from('<I', request, 20)[0] != 14012 or not request[32:].startswith(b'com.apple.locationd\0'):
        p.error('Expected the saved original locationd self query')
    out = ROOT / 'neighbor/xnu/out' / ('ios6-ls-reply-callback-' + datetime.now().strftime('%Y%m%d-%H%M%S')); out.mkdir()
    cases = [dict(name='generated stub with modeled success', boolean=1), dict(name='generated stub with modeled business error5', business_result=5),
             dict(name='reply allocation failure', allocation_failure=True), dict(name='invalid subsystem id', id=15000),
             dict(name='absent reply capability', remote=0), dict(name='generic callback with modeled MIG_NO_REPLY', business_result=0xfffffecf),
             dict(name='native send invalid destination', send_result=0x10000003),
             dict(name='native malformed request length', header_size=287),
             dict(name='native 14012 outer lookup false', native_business=True, lookup_result=0),
             dict(name='native 14012 outer lookup true', native_business=True, lookup_result=1),
             dict(name='native 14012 outer imports return MIG_NO_REPLY', native_business=True, lookup_result=0, dependency_result=0xfffffecf),
             dict(name='native 14012 outer nested lookup returns MIG_NO_REPLY', native_business=True, lookup_result=0xfffffecf),
             dict(name='native 14012 outer CF string failure', native_business=True, cf_string_failure=True)]
    report = dict(source=str(source), source_sha256=hashlib.sha256(request).hexdigest(),
                  scope='original callback/demux/stub instructions; received header/trailer, CF/ObjC, VM, lookup and IPC modeled', cases=[])
    with (ROOT / 'assets/ios6-extract/dyld_cache6_armv7').open('rb') as file:
        with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as cache:
            for case in cases:
                value = run(cache, request, case); report['cases'].append(value)
                (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
                print(json.dumps(value), flush=True)
    success, error, allocation, unknown, absent, no_reply, send_error, malformed = report['cases'][:8]
    assert len(success['sends']) == 1 and success['sends'][0]['id'] == 14112 and success['sends'][0]['size'] == 40
    assert success['sends'][0]['output_boolean'] == 1 and success['sends'][0]['retcode'] == 0
    assert len(error['sends']) == 1 and error['sends'][0]['retcode'] == 5 and error['sends'][0]['size'] == 36
    assert all(not case['sends'] for case in (allocation, unknown, absent, no_reply))
    assert len(send_error['sends']) == 1 and 'mach_msg_destroy' in send_error['calls']
    assert len(malformed['sends']) == 1 and malformed['sends'][0]['retcode'] == 0xfffffed0
    assert not malformed['reached'] and malformed['sends'][0]['size'] == 36
    for value, boolean in zip(report['cases'][8:], (0, 1, 0, 0xfffffecf, 0)):
        assert value['business_outer'] == 'original instructions'
        assert 'native unconditional RetCode=0 return' in value['reached']
        assert len(value['sends']) == 1 and value['sends'][0]['retcode'] == 0
        assert value['sends'][0]['size'] == 40 and value['sends'][0]['output_boolean'] == boolean
    report['passed'] = True; (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS original callback branches, not physical reply delivery'); print(out)


if __name__ == '__main__': main()
