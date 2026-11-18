#!/usr/bin/env python3
"""Cross-link a default-off iOS6 pdev with audited symbol-only import interfaces.

The generated import libraries are never deployment libraries and never run.
Only the bundle is materialized; hardware enable and signing remain separate.
"""
import hashlib
import importlib.util
import json
import mmap
import os
import plistlib
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ROOT = REPO.parent
sys.path.insert(0, str(REPO / "port-tests"))
from i9100_offline_build import Evidence, PLATFORM


def symbols(path, undefined=False):
    raw = Path(path).read_bytes()
    if struct.unpack_from("<II", raw) != (0xfeedface, 12):
        raise RuntimeError("Expected an ARM32 Mach-O")
    at = 28
    result = {}
    for _ in range(struct.unpack_from("<I", raw, 16)[0]):
        cmd, size = struct.unpack_from("<II", raw, at)
        if cmd == 2:
            table, n, strings, length = struct.unpack_from("<4I", raw, at + 8)
            for i in range(n):
                off, kind, _, desc, value = struct.unpack_from("<IBBHI", raw, table + i * 12)
                end = raw.find(b"\0", strings + off, strings + length)
                name = raw[strings + off:end].decode()
                if kind & 1 and (kind & 0xe == 0 if undefined else kind & 0xe == 0xe):
                    result[name] = dict(value=value, desc=desc)
        at += size
    return result


def main():
    e = Evidence("audio-pdev-arm")
    e.wait_idle_seconds = 30
    original = {name: (HERE / name).read_bytes() for name in ("i9100-pdev.c", "i9100-pdev.h", "Info.plist")}
    object_path = e.out / "I9100Audio.o"
    compiler = ["clang", "--target=armv7-apple-ios6.1", "-mthumb", "-mfloat-abi=softfp", "-mfpu=neon",
                "-ffreestanding", "-fno-stack-protector", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I" + str(PLATFORM), "-I" + str(HERE)]
    e.run([*compiler, "-c", str(HERE / "i9100-pdev.c"), "-o", str(object_path)], "arm-pdev-compile")
    imports = set(symbols(object_path, True))
    imports.add("dyld_stub_binder")
    known = {
        "CoreAudio": {"_AudioObjectCreate"},
        "CoreFoundation": {"_CFStringCreateWithCString", "_CFRetain", "_CFRelease", "_CFBundleGetBundleWithIdentifier",
                           "_CFBundleGetValueForInfoDictionaryKey", "_CFGetTypeID", "_CFBooleanGetTypeID", "_CFBooleanGetValue"},
        "IOKit": {"_IOServiceMatching", "_IOServiceGetMatchingService", "_IOServiceOpen", "_IOObjectRelease",
                  "_IOServiceClose", "_IOConnectCallStructMethod"},
    }
    grouped = {name: imports & values for name, values in known.items()}
    grouped["System"] = imports - set().union(*grouped.values())
    spec = importlib.util.spec_from_file_location("pdev_cache", ROOT / "uniloader-S2I9100/scripts/i9100/symbolize-ios6-stacks.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cache_path = ROOT / "assets/ios6-extract/dyld_cache6_armv7"
    with cache_path.open("rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        cache = module.Cache(data)
        paths = {}
        present = {}
        for base, name in cache.images:
            leaf = name.rsplit("/", 1)[-1]
            if leaf in ("CoreAudio", "CoreFoundation", "IOKit", "libSystem.B.dylib"):
                paths["System" if leaf == "libSystem.B.dylib" else leaf] = name
            if leaf in ("CoreAudio", "CoreFoundation", "IOKit", "libSystem.B.dylib") or leaf.startswith(("libsystem_", "libdyld", "libcompiler_rt")):
                for va, symbol in cache.image_symbols(base)[1]:
                    if symbol in imports:
                        present.setdefault(symbol, []).append(dict(image=name, address=hex(va)))
        missing = imports - set(present)
        if missing:
            raise RuntimeError("Imports missing from the original cache: " + repr(sorted(missing)))
        if set(paths) != set(grouped):
            raise RuntimeError("Original framework install names are incomplete")
        e.result["original_cache"] = dict(path=str(cache_path.relative_to(ROOT)), sha256=hashlib.sha256(data).hexdigest(), imports=present)
    linker = ROOT / "assets/i9100-toolchain/bin/ld64"
    interfaces = e.out / "import-interfaces"
    interfaces.mkdir()
    libraries = []
    for group, names in grouped.items():
        text = [".syntax unified", ".thumb", ".text"]
        for name in sorted(names):
            if name == "_mach_task_self_":
                continue
            text += [".globl " + name, ".thumb_func " + name, name + ":", " bx lr"]
        if "_mach_task_self_" in names:
            text += [".data", ".align 2", ".globl _mach_task_self_", "_mach_task_self_:", " .long 0"]
        assembly = interfaces / (group + ".s")
        obj = interfaces / (group + ".o")
        lib = interfaces / ("lib" + group + ".dylib")
        assembly.write_text("\n".join(text) + "\n")
        e.run(["clang", "--target=armv7-apple-ios6.1", "-mthumb", "-c", str(assembly), "-o", str(obj)], "compile-interface-" + group)
        e.run([str(linker), "-arch", "armv7", "-dylib", "-ios_version_min", "6.1", "-install_name", paths[group],
               str(obj), "-o", str(lib)], "link-interface-" + group)
        libraries.append(str(lib))
    bundle = e.out / "I9100Audio.pdev"
    bundle.mkdir()
    binary = bundle / "I9100Audio"
    e.run([str(linker), "-arch", "armv7", "-bundle", "-ios_version_min", "6.1", str(object_path), *libraries,
           "-o", str(binary)], "link-pdev-bundle")
    exports = symbols(binary)
    required = {"_PhysicalDevicePlugIn_" + name for name in (
        "Initialize", "Teardown", "PerformDeviceConfigChange", "ObjectHasProperty", "ObjectIsPropertySettable",
        "ObjectGetPropertyDataSize", "ObjectGetPropertyData", "ObjectSetPropertyData", "GetZeroTimeStamp",
        "BeginIOCycle", "EndIOCycle", "AcquireIOBufferForStream", "ReleaseIOBufferForStream", "ReadFromStream", "WriteToStream")}
    if set(exports) & required != required or any(not exports[name]["desc"] & 8 for name in required):
        raise RuntimeError("Original pdev exports or Thumb flags are incomplete")
    info = plistlib.loads(original["Info.plist"])
    if info["I9100AudioPDevEnabled"] is not False:
        raise RuntimeError("Offline bundle must be disabled by default")
    (bundle / "Info.plist").write_bytes(original["Info.plist"])
    for name, data in original.items():
        if (HERE / name).read_bytes() != data:
            raise RuntimeError("Pdev input changed while building: " + name)
    e.result.update(bundle=str(bundle.relative_to(ROOT)), bundle_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    bundle_bytes=binary.stat().st_size, exports=exports, import_interfaces_only=True, signed=False,
                    installed=False, enabled=False, hardware_tested=False,
                    pdev_sources={name: hashlib.sha256(data).hexdigest() for name, data in original.items()})
    e.finish([], "ARMv7 iOS6 pdev compiled/linked with original-cache-audited symbol interfaces; default OFF, unsigned, not installed, no hardware execution")


if __name__ == "__main__":
    main()
