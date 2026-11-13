#!/usr/bin/env python3
"""Execute linked ARM sleep policy and sync callback with external calls modeled.

This verifies the actual kernel instructions, not physical wake or filesystem
completion. The RX text mapping remains protected during execution.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    a = parser.parse_args()
    workspace = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('memory',
        workspace/'uniloader-S2I9100/scripts/i9100/live-memory.py')
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    symbols = m.Symbols(a.kernel)
    data = a.kernel.read_bytes()
    pos, segments = 28, []
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<2I', data, pos)
        if command == 1:
            segments.append(struct.unpack_from('<8I', data, pos+24))
        pos += size
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    base = min(v for v, n, *_ in segments) & ~4095
    end = max(v+n for v, n, *_ in segments)
    u.mem_map(base, (end-base+4095) & ~4095)
    for va, size, offset, length, maximum, initial, _, _ in segments:
        if length: u.mem_write(va, data[offset:offset+length])
        if size: u.mem_protect(va, (size+4095) & ~4095, initial)
    u.mem_map(0x10000000, 0x10000)
    u.mem_map(0x20000000, 0x10000)
    u.mem_map(0x70000000, 0x1000)
    obj, vtable, stop = 0x10001000, 0x10002000, 0x70000000
    u.mem_write(obj, struct.pack('<I', vtable))
    u.mem_write(vtable+0x490, struct.pack('<I', stop+0x21))
    callees = {symbols['_sync_internal']: 'sync',
        symbols['__ZN9IOService13getPowerStateEv']: 'powerstate', stop+0x20: 'ack'}
    forbidden = {symbols['_IOHibernateSystemSleep'], symbols['_IOHibernateSystemPostWake']}
    events, checks = [], []
    state = 3

    def code(machine, address, size, unused):
        if address == stop:
            machine.emu_stop(); return
        if address in forbidden:
            raise AssertionError('Unimplemented hibernate path reached')
        if address in callees:
            kind = callees[address]; events.append(kind)
            if kind == 'ack':
                assert machine.reg_read(UC_ARM_REG_R0) == obj
                assert machine.reg_read(UC_ARM_REG_R1) == 0x12345678
            machine.reg_write(UC_ARM_REG_R0, state if kind == 'powerstate' else 0)
            machine.reg_write(UC_ARM_REG_PC, machine.reg_read(UC_ARM_REG_LR))

    u.hook_add(UC_HOOK_CODE, code)
    registers = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12]

    def call(name, arguments):
        for reg in registers: u.reg_write(reg, 0)
        for reg, value in zip(registers, arguments): u.reg_write(reg, value)
        u.reg_write(UC_ARM_REG_CPSR, 0x13)
        u.reg_write(UC_ARM_REG_SP, 0x2000f000)
        u.reg_write(UC_ARM_REG_LR, stop+1)
        events.clear()
        u.emu_start(symbols[name] | 1, 0, count=2000)
        assert u.reg_read(UC_ARM_REG_PC) == stop
        return u.reg_read(UC_ARM_REG_R0), list(events)

    for option in (0, 1, 0xffffffff):
        result, called = call('__ZN14IOPMrootDomain19checkSystemCanSleepEm', [obj, option])
        assert result == 0 and not called
        checks.append(dict(sleep_options=option, result=result))
    for state in (3, 0, 1):
        result, called = call('__ZL17disk_sync_calloutPvS_', [obj, 0x12345678])
        assert called == (['powerstate', 'sync', 'ack'] if state == 3 else ['powerstate', 'ack'])
        checks.append(dict(power_state=state, actual_calls=called))
    out = a.output.resolve()
    assert out.is_relative_to(workspace) and not out.exists()
    out.mkdir(parents=True)
    (out/'result.json').write_text(json.dumps(dict(kernel_sha256=hashlib.sha256(data).hexdigest(),
        checks=checks, limitation='Linked ARM policy/callback with external IOService calls modeled; '
        'does not prove physical wake or filesystem sync completion.'), indent=2)+'\n', encoding='utf-8')
    print('PASS linked ARM sleep denial and sync/ack branches', checks)


if __name__ == '__main__':
    main()
