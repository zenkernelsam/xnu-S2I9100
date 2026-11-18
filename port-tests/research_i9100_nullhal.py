#!/usr/bin/env python3
"""Execute bounded original NullHAL property queries, with explicit host stubs.

This establishes the old property ABI only. Its IO success and software clock
are not device evidence and are never used as a playback implementation.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path
from i9100_offline_build import ROOT, cap_cpu


def main():
    cap_cpu()
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    if not out.is_relative_to(ROOT / "research") or out.exists():
        p.error("A fresh project research directory is required")
    out.mkdir()
    path = ROOT / "research/audio-hal-20261118/NullHALPlugin"
    raw = path.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    if sha != "4afbbe26cd641681255b932086a9089d3b250001fa50cc6ba3fb6bebb243754c":
        raise ValueError("Original NullHAL image changed")
    sys.path.insert(0, str(ROOT / "assets/py-native-hid"))
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    u.mem_map(0, 0x80000)
    _, cpu, _, _, ncmds, _, _ = struct.unpack_from("<7I", raw)
    if cpu != 12:
        raise ValueError("Expected original ARM32 plugin")
    sections, symtab, dysymtab = [], None, None
    at = 28
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", raw, at)
        if cmd == 1:
            va, n, off, fsize = struct.unpack_from("<4I", raw, at + 24)
            if va + n > 0x10000:
                raise ValueError("Unexpected original image extent")
            if fsize:
                u.mem_write(va, raw[off:off + fsize])
            for i in range(struct.unpack_from("<I", raw, at + 48)[0]):
                s = raw[at + 56 + i * 68:at + 56 + (i + 1) * 68]
                fields = struct.unpack_from("<9I", s, 32)
                sections.append((s[:16].rstrip(b"\0").decode(), *fields))
        if cmd == 2:
            symtab = struct.unpack_from("<4I", raw, at + 8)
        if cmd == 0xb:
            dysymtab = struct.unpack_from("<18I", raw, at + 8)
        at += size
    if not symtab or not dysymtab:
        raise ValueError("Missing original import metadata")
    so, sn, strings, ssize = symtab
    names, exports = [], {}
    for i in range(sn):
        noff, kind, _, _, value = struct.unpack_from("<IBBHI", raw, so + i * 12)
        end = raw.find(b"\0", strings + noff, strings + ssize)
        name = raw[strings + noff:end].decode()
        names.append(name)
        if kind & 0xe == 0xe and name.startswith("_PhysicalDevicePlugIn_"):
            exports[name] = value
    indirect, count = dysymtab[12:14]
    stubs = {}
    for _, va, n, _, _, _, _, flags, reserved1, reserved2 in sections:
        if flags & 0xff != 8:
            continue
        for i in range(n // reserved2):
            index = struct.unpack_from("<I", raw, indirect + (reserved1 + i) * 4)[0]
            if index >= len(names) or reserved1 + i >= count:
                raise ValueError("Invalid original stub symbol")
            stubs[va + i * reserved2] = names[index]
    regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
    created, calls = [], []
    host, prop, sizeptr, buf, stack, ret = 0x10000, 0x11000, 0x12000, 0x13000, 0x70000, 0x7f000

    def words(address, count=1):
        return struct.unpack("<" + "I" * count, u.mem_read(address, count * 4))

    def write(address, *values):
        u.mem_write(address, struct.pack("<" + "I" * len(values), *values))

    def hook(_, address, _size, _cookie):
        if address == ret:
            u.emu_stop()
            return
        if address not in stubs and not 0x50000 <= address < 0x50020:
            return
        args = [u.reg_read(x) for x in regs] + list(words(u.reg_read(UC_ARM_REG_SP), 2))
        name = stubs.get(address, "host-callback-" + hex(address - 0x50000))
        result = 0
        if name == "_malloc":
            if args[0] != 0x4000:
                raise ValueError("Unexpected null buffer allocation")
            result = 0x20000
        elif name == "_AudioObjectCreate":
            obj = 100 + len(created)
            created.append(dict(owner=args[1], class_id=args[2].to_bytes(4, "big").decode(), object=obj))
            write(args[3], obj)
        elif name == "_free" or name.startswith("host-callback-"):
            pass
        elif name == "_CFBundleGetBundleWithIdentifier":
            # No framework/bundle is present in this execution model. Nil
            # tests the original fallback; it cannot establish its live value.
            result = 0
        elif name in ("_memset", "_memcpy"):
            if args[2] > 4096 or not 0x10000 <= args[0] < 0x70000 or args[0] + args[2] > 0x70000:
                raise ValueError("Original memory helper escaped its bounded fixture")
            value = bytes([args[1] & 255]) * args[2] if name == "_memset" else bytes(u.mem_read(args[1], args[2]))
            u.mem_write(args[0], value)
            result = args[0]
        else:
            raise ValueError("Unexpected original external call: " + name)
        calls.append(dict(name=name, args=args, result=result))
        u.reg_write(UC_ARM_REG_R0, result)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    u.hook_add(UC_HOOK_CODE, hook)
    write(host, 0x50001, 0x50005, 0x50009, 0x5000d, 42, 0x15000, 0x50019)

    def call(name, args):
        write(stack, *args[4:], *([0] * 8))
        for reg, value in zip(regs, args[:4] + [0] * (4 - min(4, len(args)))):
            u.reg_write(reg, value)
        u.reg_write(UC_ARM_REG_SP, stack)
        u.reg_write(UC_ARM_REG_LR, ret | 1)
        u.emu_start(exports[name] | 1, ret, count=20000)
        if u.reg_read(UC_ARM_REG_PC) != ret:
            raise ValueError("Original function did not return within its bound")
        return u.reg_read(UC_ARM_REG_R0)

    if call("_PhysicalDevicePlugIn_Initialize", [host]):
        raise ValueError("Original initialization failed")
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.skipdata = True
    constants, literals = set(), {}
    text = next(s for s in sections if s[0] == "__text")
    for ins in md.disasm(bytes(u.mem_read(text[1], text[2])), text[1]):
        m = re.fullmatch(r"(r[0-9]+|sl|fp|sb|ip), #(0x[0-9a-f]+|[0-9]+)", ins.op_str)
        if ins.mnemonic == "movw" and m:
            literals[m[1]] = int(m[2], 0)
        elif ins.mnemonic == "movt" and m and m[1] in literals:
            v = literals[m[1]] | int(m[2], 0) << 16
            if all(32 <= c < 127 for c in v.to_bytes(4, "big")):
                constants.add(v)
    properties = []
    for object_id in [100, 101, 102]:
        for scope in [int.from_bytes(x, "big") for x in (b"glob", b"inpt", b"outp")]:
            for selector in sorted(constants):
                write(prop, selector, scope, 0)
                if not call("_PhysicalDevicePlugIn_ObjectHasProperty", [object_id, prop]):
                    continue
                write(sizeptr, 0xabad1dea)
                size_error = call("_PhysicalDevicePlugIn_ObjectGetPropertyDataSize", [object_id, prop, 0, 0, sizeptr])
                n = words(sizeptr)[0]
                write(sizeptr, 512)
                u.mem_write(buf, bytes([0xaa]) * 512)
                # The original selector-control item-name property consumes
                # AudioValueTranslation, not a flat CFString output buffer.
                if selector == int.from_bytes(b"lscn", "big"):
                    write(0x14000, 0)
                    write(0x14010, 0)
                    write(buf, 0x14000, 4, 0x14010, 4)
                try:
                    error = call("_PhysicalDevicePlugIn_ObjectGetPropertyData", [object_id, prop, 0, 0, sizeptr, buf])
                except Exception as exc:
                    failure = dict(error=str(exc), object=object_id, selector=selector.to_bytes(4, "big").decode(),
                                   scope=scope.to_bytes(4, "big").decode(), size=n,
                                   pc=hex(u.reg_read(UC_ARM_REG_PC)), sp=hex(u.reg_read(UC_ARM_REG_SP)),
                                   registers=[hex(u.reg_read(x)) for x in regs], properties=properties, calls=calls)
                    (out / "failure.json").write_text(json.dumps(failure, indent=2) + "\n")
                    raise
                written = words(sizeptr)[0]
                properties.append(dict(object=object_id, selector=selector.to_bytes(4, "big").decode(),
                                       scope=scope.to_bytes(4, "big").decode(), size_error=hex(size_error), size=n,
                                       data_error=hex(error), written=written,
                                       data=bytes(u.mem_read(buf, min(512, written))).hex() if not error else None,
                                       translated_value=hex(words(0x14010)[0]) if selector == int.from_bytes(b"lscn", "big") else None))
    result = dict(original=str(path.relative_to(ROOT)), sha256=sha, modeled_external_calls=calls, objects=created,
                  properties=properties, scope="Original ARM NullHAL property ABI with host stubs; no audio execution or physical proof")
    (out / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(dict(output=str(out), imports=stubs, created=created, properties=len(properties)), indent=2))


if __name__ == "__main__":
    main()
