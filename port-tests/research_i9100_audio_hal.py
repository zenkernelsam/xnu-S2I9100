#!/usr/bin/env python3
"""Read the original iOS6 physical-device ABI; never edit or load its images.

PC-relative references are discovery candidates, not recovered C prototypes.
Retain both the original bytes and consumer instructions before using an ABI.
"""
import argparse
import hashlib
import importlib.util
import json
import mmap
import re
import struct
import sys
from pathlib import Path
from i9100_offline_build import ROOT, cap_cpu


def main():
    cap_cpu()
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--xrefs", action="store_true")
    p.add_argument("--indirect-slots", action="store_true", help="Find candidate calls through the original pdev slots")
    p.add_argument("--native", action="append", default=[], metavar="VA:BYTES")
    p.add_argument("--words", action="append", default=[], metavar="VA:COUNT")
    a = p.parse_args()
    out = a.output.resolve()
    if not out.is_relative_to(ROOT / "research") or out.exists():
        p.error("Use a fresh directory in project research")
    here = ROOT / "uniloader-S2I9100/scripts/i9100"
    spec = importlib.util.spec_from_file_location("audio_original_cache", here / "symbolize-ios6-stacks.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.path.insert(0, str(ROOT / "assets/py-native-hid"))
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
    out.mkdir()
    path = ROOT / "assets/ios6-extract/dyld_cache6_armv7"
    with path.open("rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        c = module.Cache(data)
        base, image = next((v, n) for v, n in c.images if n.rsplit("/", 1)[-1] == "CoreAudio")

        def read(va, n):
            at = c.offset(va, n)
            return data[at:at + n]

        at = base + 28
        count = struct.unpack_from("<I", read(base, 28), 16)[0]
        sections = []
        uuid = None
        for _ in range(count):
            cmd, size = struct.unpack("<II", read(at, 8))
            if size < 8:
                raise ValueError("Invalid original Mach-O command")
            raw = read(at, size)
            if cmd == 0x1b and size == 24:
                uuid = raw[8:24].hex()
            if cmd == 1:
                number = struct.unpack_from("<I", raw, 48)[0]
                if size != 56 + number * 68:
                    raise ValueError("Invalid original section table")
                for i in range(number):
                    sec = raw[56 + i * 68:56 + (i + 1) * 68]
                    va, n = struct.unpack_from("<II", sec, 32)
                    sections.append(dict(name=sec[:16].rstrip(b"\0").decode(),
                                         segment=sec[16:32].rstrip(b"\0").decode(), va=va, bytes=n))
            at += size
        _, symbols, _ = c.image_symbols(base)
        manifest = dict(image=image, uuid=uuid, sections=sections, source=str(path.relative_to(ROOT)),
                        source_sha256=hashlib.sha256(data).hexdigest(), read_only=True, hardware_tested=False,
                        symbols=[dict(name=n, va=hex(v)) for v, n in symbols
                                 if re.search("Physical|PlugIn|AudioObjectCreate|AudioObjectDestroy|AudioObjectProperties", n)])
        targets = {}
        for s in sections:
            if s["name"] != "__cstring":
                continue
            raw = read(s["va"], s["bytes"])
            for match in re.finditer(rb"PhysicalDevicePlugIn_[A-Za-z0-9]+|/Library/Audio/Plug-Ins/HAL", raw):
                targets[s["va"] + match.start()] = match[0].decode()
        for s in sections:
            if s["name"] != "__cfstring":
                continue
            for off in range(0, s["bytes"] - 15, 16):
                _, flags, string, _ = struct.unpack("<IIII", read(s["va"] + off, 16))
                if string in targets and flags == 0x7c8:
                    targets[s["va"] + off] = "CFString:" + targets[string]
        manifest["abi_strings"] = [dict(va=hex(v), name=n) for v, n in sorted(targets.items())]
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        md.skipdata = True
        references = []

        def discovery(ins, regs):
            key = re.fullmatch(r"(r[0-9]+|sl|fp|sb|ip), #(0x[0-9a-f]+|[0-9]+)", ins.op_str)
            value = None
            reg = ins.op_str.split(",", 1)[0]
            if ins.mnemonic == "movw" and key:
                regs[reg] = (int(key[2], 0), ins.address)
            elif ins.mnemonic == "movt" and key and reg in regs:
                old, start = regs[reg]
                if ins.address - start <= 32:
                    regs[reg] = ((old & 0xffff) | int(key[2], 0) << 16, start)
            elif ins.mnemonic == "ldr" and "[pc," in ins.op_str:
                literal = re.search(r"\[pc, #(0x[0-9a-f]+|[0-9]+)\]", ins.op_str)
                if literal:
                    try:
                        value = struct.unpack("<I", read(((ins.address + 4) & ~3) + int(literal[1], 0), 4))[0]
                        regs[reg] = (value, ins.address)
                    except ValueError:
                        pass
            elif ins.mnemonic in ("add", "add.w") and ins.op_str.endswith(", pc") and reg in regs:
                old, start = regs.pop(reg)
                if ins.address - start <= 40:
                    value = (old + ins.address + 4) & 0xffffffff
            if value in targets:
                return dict(address=hex(ins.address), bytes=ins.bytes.hex(), mnemonic=ins.mnemonic,
                            operands=ins.op_str, target=hex(value), name=targets[value], candidate=True)
            return None

        if a.xrefs:
            for s in sections:
                if s["name"] != "__text":
                    continue
                regs = {}
                for ins in md.disasm(read(s["va"], s["bytes"]), s["va"]):
                    found = discovery(ins, regs)
                    if found:
                        references.append(found)
            manifest["reference_candidates"] = references
        if a.indirect_slots:
            calls = []
            for s in sections:
                if s["name"] != "__text":
                    continue
                instructions = list(md.disasm(read(s["va"], s["bytes"]), s["va"]))
                for i, ins in enumerate(instructions):
                    match = re.fullmatch(r"(r[0-9]+|sl|fp|sb|ip), \[r0, #(0x[0-9a-f]+|[0-9]+)\]", ins.op_str)
                    if not match or ins.mnemonic not in ("ldr", "ldr.w") or int(match[2], 0) not in range(0x28, 0x64, 4):
                        continue
                    window = instructions[max(0, i - 4):i + 9]
                    if any(x.mnemonic in ("bx", "blx") and x.op_str == match[1] for x in instructions[i + 1:i + 9]):
                        calls.append(dict(address=hex(ins.address), slot=match[2], candidate=True,
                                          instructions=[f"{x.address:08x} {x.bytes.hex()} {x.mnemonic} {x.op_str}" for x in window]))
            manifest["indirect_slot_candidates"] = calls
        manifest["word_windows"] = []
        for arg in a.words:
            va, n = (int(x, 0) for x in arg.split(":"))
            if not 1 <= n <= 256 or va & 3:
                raise ValueError("Word window must be aligned and bounded")
            values = struct.unpack("<" + "I" * n, read(va, n * 4))
            manifest["word_windows"].append(dict(va=hex(va), values=[hex(x) for x in values]))
        lines = []
        for arg in a.native:
            va, n = (int(x, 0) for x in arg.split(":"))
            if not 2 <= n <= 32768 or va & 1 or not any(s["segment"] == "__TEXT" and s["va"] <= va
                                                       and va + n <= s["va"] + s["bytes"] for s in sections):
                raise ValueError("Native window must fit one original text section")
            blob = read(va, n)
            (out / (f"native-{va:08x}.bin")).write_bytes(blob)
            lines.append(f"ORIGINAL {va:08x} bytes={n} SHA256={hashlib.sha256(blob).hexdigest()}")
            regs = {}
            for ins in md.disasm(blob, va):
                found = discovery(ins, regs)
                note = " ; " + found["name"] if found else ""
                lines.append(f"{ins.address:08x} {ins.bytes.hex():12} {ins.mnemonic:10} {ins.op_str}{note}")
        (out / "native.txt").write_text("\n".join(lines) + "\n")
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(json.dumps(dict(output=str(out), strings=len(targets), candidates=references,
                              indirect_candidates=len(manifest.get("indirect_slot_candidates", [])),
                              words=manifest["word_windows"], uuid=uuid, symbols=manifest["symbols"]), indent=2))


if __name__ == "__main__":
    main()
