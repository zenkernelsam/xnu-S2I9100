#!/usr/bin/env python3
"""Inspect direct call wiring in a saved ARM Mach-O, without running the kernel."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
        sys.executable, '-B', __file__, *sys.argv[1:]], dict(os.environ, I9100_CPU_CAPPED='1'))

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
from capstone.arm import ARM_INS_BL, ARM_INS_BLX, ARM_INS_B, ARM_OP_IMM

REPO = Path(__file__).resolve().parents[1]


def inspect(path, caller, callees):
    data = path.read_bytes()
    magic, cpu, _, _, commands, command_bytes, _ = struct.unpack_from('<7I', data)
    if magic != 0xfeedface or cpu != 12 or commands > 1024 or command_bytes > len(data)-28:
        raise ValueError('Expected a bounded ARM32 Mach-O command table')
    at = 28
    sections = {}
    symtab = None
    for _ in range(commands):
        command, size = struct.unpack_from('<2I', data, at)
        if size < 8 or at+size > 28+command_bytes:
            raise ValueError('Invalid Mach-O load command')
        if command == 1:
            count = struct.unpack_from('<I', data, at+48)[0]
            if size != 56+count*68:
                raise ValueError('Invalid ARM segment section geometry')
            for index in range(count):
                offset = at+56+index*68
                address, length, fileoff = struct.unpack_from('<3I', data, offset+32)
                sections[len(sections)+1] = dict(address=address, size=length, fileoff=fileoff,
                    name=data[offset:offset+16].split(b'\0')[0].decode('ascii'))
        elif command == 2:
            symtab = struct.unpack_from('<4I', data, at+8)
        at += size
    if symtab is None:
        raise ValueError('Kernel lacks its symbol table')
    offset, count, strings_at, strings_size = symtab
    if offset+12*count > len(data) or strings_at+strings_size > len(data):
        raise ValueError('Symbol table exceeds saved image')
    symbols = {}
    for index in range(count):
        name_at, kind, section, desc, value = struct.unpack_from('<IBBHI', data, offset+index*12)
        if kind & 0xe0 or kind & 0x0e != 0x0e or not value or name_at >= strings_size:
            continue
        end = data.index(b'\0', strings_at+name_at, strings_at+strings_size)
        name = data[strings_at+name_at:end].decode('ascii')
        symbols[name] = dict(address=value & ~1, section=section, thumb=bool(desc & 8))
    entry = symbols[caller]
    section = sections[entry['section']]
    if section['name'] != '__text':
        raise ValueError('Caller is outside executable text')
    start = entry['address']
    end = min([section['address']+section['size']] + [value['address'] for name,value in symbols.items()
        if name.startswith('_') and value['section']==entry['section'] and value['address']>start])
    if not 0 < end-start <= 131072:
        raise ValueError('Unbounded caller extent')
    fileoff = section['fileoff']+start-section['address']
    if fileoff < 0 or fileoff+end-start > len(data):
        raise ValueError('Caller is outside file-backed text')
    decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB if entry['thumb'] else CS_MODE_ARM)
    decoder.detail = True
    decoder.skipdata = True
    assembly = []
    calls = []
    for instruction in decoder.disasm(data[fileoff:fileoff+end-start], start):
        assembly.append(f'{instruction.address:08x} {instruction.bytes.hex():12} {instruction.mnemonic:8} {instruction.op_str}')
        if instruction.id in (ARM_INS_BL, ARM_INS_BLX, ARM_INS_B) and instruction.operands[0].type == ARM_OP_IMM:
            calls.append(dict(address=instruction.address, opcode=instruction.bytes.hex(),
                instruction=instruction.mnemonic, target=instruction.operands[0].imm & 0xfffffffe))
    requested = {}
    for name in callees:
        target = symbols.get(name)
        requested[name] = dict(symbol_present=bool(target), address=target['address'] if target else None,
            direct_calls=[call for call in calls if target and call['target']==target['address']])
    result = dict(kernel=str(path), kernel_sha256=hashlib.sha256(data).hexdigest(), caller=caller,
        start=start, end=end, mode='Thumb' if entry['thumb'] else 'ARM',
        extent='Next named text symbol; inspect assembly for literal pools/aliases',
        callees=requested, decoded_direct_branches=calls,
        limit='Static linked branches only. Indirect dispatch and actual execution are not proved.')
    return result, '\n'.join(assembly)+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--caller', default='_unix_syscall')
    parser.add_argument('--callee', action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    kernel, output = args.kernel.resolve(), args.output.resolve()
    if not kernel.is_relative_to(REPO.parent) or not output.is_relative_to(REPO/'out') or output.exists():
        parser.error('Project input and fresh XNU out evidence required')
    result, assembly = inspect(kernel, args.caller, args.callee)
    output.mkdir(parents=True)
    (output/'calls.json').write_text(json.dumps(result, indent=2)+'\n')
    (output/'caller.s.txt').write_text(assembly)
    print(json.dumps({key:result[key] for key in ('kernel_sha256','caller','mode','start','end','callees')}, indent=2))


if __name__ == '__main__':
    main()
