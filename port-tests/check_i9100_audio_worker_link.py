#!/usr/bin/env python3
"""Reproduce a verified full kernel, then replace/append explicit audio objects.

Keeps all prior peripheral and paired Power objects; never edits shared BUILD,
live integration patches, loader packages or hardware. Not a production build.
The default still replaces only board/worker; opt-in lists support PCM/client
updates against retained complete references, including frozen-path ancestry.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import struct
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, idle
from i9100_link_snapshot import freeze, verify

CHANGED = ("i9100_audio_board_native.cpp", "i9100_audio_worker.cpp")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def uuid_offset(data):
    if len(data) < 28 or struct.unpack_from("<I", data)[0] != 0xfeedface:
        raise RuntimeError("Expected a 32-bit Mach-O reference")
    count, total = struct.unpack_from("<II", data, 16)
    pos, found = 28, []
    if pos + total > len(data):
        raise RuntimeError("Mach-O command area is truncated")
    for _ in range(count):
        cmd, size = struct.unpack_from("<II", data, pos)
        if size < 8 or pos + size > 28 + total:
            raise RuntimeError("Invalid Mach-O command")
        if cmd == 0x1b:
            if size != 24:
                raise RuntimeError("Invalid LC_UUID")
            found.append(pos + 8)
        pos += size
    if pos != 28 + total or len(found) != 1:
        raise RuntimeError("LC_UUID is missing or ambiguous")
    return found[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--base", type=Path, required=True)
    p.add_argument("--replace", action="append", default=[], metavar="SOURCE", help="Replace a uniquely retained peripheral object")
    p.add_argument("--append", action="append", default=[], metavar="SOURCE", help="Append a new opt-in object")
    p.add_argument("--wait-idle", type=int, default=60, choices=range(61))
    p.add_argument("--sensor-hid-service", action="store_true", help="Compile the explicit sensor HID extension only on a source copy")
    p.add_argument("--emmc-storage",action="store_true",help="Compile exact BSD media/root alias integration only on source copies")
    p.add_argument("--hfs-boot",action="store_true",help="Compile exact VFS/HFS/prelaunchd and storage-safe USB shutdown source copies")
    p.add_argument('--emmc-boot',action='store_true',help='Compile optional internal-root PE selection only on a source copy')
    args = p.parse_args()
    changed=tuple(args.replace) if args.replace else () if args.append else CHANGED
    added=tuple(args.append)
    if args.sensor_hid_service and "IOS7LeoHIDTouch.cpp" not in changed:
        p.error("--sensor-hid-service requires --replace IOS7LeoHIDTouch.cpp")
    if args.emmc_storage and not {"IOMediaBSDClient.cpp","IOKitBSDInit.cpp"}.issubset(changed):
        p.error("--emmc-storage requires paired IOMediaBSDClient.cpp and IOKitBSDInit.cpp replacements")
    hfs_names={"vfs_syscalls.c","hfs_vfsops.c","bsd_init.c","kern_shutdown.c","i9100_usbcdc.c"}
    if args.hfs_boot and not hfs_names.issubset(changed):
        p.error("--hfs-boot requires all five actual BSD/USB source replacements")
    if args.emmc_boot and 'AppleARMPE.cpp' not in changed:p.error('--emmc-boot requires --replace AppleARMPE.cpp')
    if len(set(changed+added))!=len(changed+added) or any(
            not re.fullmatch(r"[A-Za-z0-9_]+\.(c|cpp)", name) or not ((REPO/"iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform"/name).is_file()
            or args.emmc_storage and name in ("IOMediaBSDClient.cpp","IOKitBSDInit.cpp")
            or args.hfs_boot and name in hfs_names)
            for name in changed+added):
        raise RuntimeError("Use unique, existing platform source names")
    base = args.base.resolve()
    if not base.is_relative_to(REPO / "out"):
        raise RuntimeError("Reference must remain in project out")
    manifest = json.loads((base / "manifest.json").read_text())
    expected = manifest.get("linked_kernel_sha256")
    if manifest.get("result") != "PASS" or not manifest.get("complete_kernel_link") or not expected:
        raise RuntimeError("A verified complete peripheral reference is required")
    if digest(base / "mach_kernel.sys") != expected:
        raise RuntimeError("Reference kernel bytes changed")
    previous_inputs = [verify(ROOT / name) for name in manifest["frozen_input_manifests"]]
    aux_sets=[items for items in previous_inputs if len(items)==2 and all(
        x["frozen"].endswith(suffix) for x,suffix in zip(items,("version.o","lastkernelconstructor.o")))]
    if len(aux_sets)!=1:
        raise RuntimeError("Auxiliary constructor/version inputs are ambiguous")
    aux=aux_sets[0]
    controls=manifest;seen={base};cursor=base
    for _ in range(8):
        if controls.get("kernel_reference"):break
        cursor=(ROOT/controls["base"]).resolve()
        if cursor in seen or not cursor.is_relative_to(REPO/"out"):
            raise RuntimeError("Invalid retained reference ancestry")
        seen.add(cursor);controls=json.loads((cursor/"manifest.json").read_text())
    else:raise RuntimeError("Retained link controls could not be located")
    reference = (ROOT / controls["kernel_reference"]).resolve()
    if not reference.is_relative_to(REPO / "out"):
        raise RuntimeError("Link controls escaped project out")
    flags = reference / "link-flags.txt"
    flags_sha = digest(flags)
    command = shlex.split(flags.read_text().strip().splitlines()[-1])
    build = REPO / "BUILD/obj/RELEASE_ARM_EXYNOS4210_I9100"
    env = dict(os.environ, TC=str(ROOT / "assets/i9100-toolchain"), SDKROOT="/",
               LD64_WRAPPER=str(ROOT / "assets/i9100-toolchain/bin/ld64"), SRCROOT=str(REPO),
               OBJROOT=str(REPO / "BUILD/obj"), SYMROOT=str(REPO / "BUILD/sym"), TARGET=str(build))
    env["PATH"] = str(ROOT / "assets/i9100-toolchain/bin") + ":" + env["PATH"]
    e = Evidence("audio-client-link" if args.replace or added else "audio-worker-link")
    e.wait_idle_seconds = args.wait_idle
    inputs = (base / "link.filelist").read_text().splitlines()
    if len(inputs) != len(set(inputs)):
        raise RuntimeError("Reference filelist contains duplicates")
    idle()
    captured, snapshot = freeze(inputs, e.out / "frozen-reference")
    frozen_aux, aux_snapshot = freeze([ROOT / x["frozen"] for x in aux], e.out / "frozen-aux")
    baseline_list = e.out / "baseline.filelist"
    # ld64 emits input-object paths into the stab/string table. Reproduce
    # with the exact original path spelling, while separately snapshotting
    # and checking those input bytes; a renamed frozen path changes LINKEDIT.
    baseline_list.write_text("\n".join(inputs) + "\n")
    reproduced = e.out / "baseline-reproduced.sys"
    e.run([*command, "-filelist", str(baseline_list), *[str(ROOT / x["frozen"]) for x in aux], "-o", str(reproduced)],
          "reproduce-reference-kernel", env, cwd=build)
    original = (base / "mach_kernel.sys").read_bytes()
    result = reproduced.read_bytes()
    uuid_at = uuid_offset(original)
    if len(result) != len(original) or uuid_offset(result) != uuid_at:
        raise RuntimeError("Reference Mach-O layout no longer reproduces")
    normalized = bytearray(result)
    normalized[uuid_at:uuid_at + 16] = original[uuid_at:uuid_at + 16]
    if hashlib.sha256(normalized).hexdigest() != expected:
        raise RuntimeError("Reference changed beyond the nondeterministic LC_UUID")
    (e.out / "baseline-uuid-normalized.sys").write_bytes(normalized)
    e.result["uuid_normalization"] = dict(offset=uuid_at, reference=original[uuid_at:uuid_at + 16].hex(),
                                           reproduced=result[uuid_at:uuid_at + 16].hex(),
                                           raw_reproduced_sha256=digest(reproduced), normalized_sha256=expected)
    for item in verify(snapshot):
        if digest(ROOT / item["source"]) != item["sha256"]:
            raise RuntimeError("Original reference input changed during reproduction")
    overrides=None
    if args.sensor_hid_service:
        from i9100_sensor_hid_service_copy import prepare as prepare_sensor_service, verify as verify_sensor_service
        overrides=prepare_sensor_service(e)
    if args.emmc_storage:
        from i9100_emmc_storage_copy import prepare as prepare_emmc_service,verify as verify_emmc_service
        copies=prepare_emmc_service(e)
        overrides=dict(overrides or {},**copies)
    hfs_copies={}
    if args.emmc_boot:
        from i9100_emmc_boot_copy import prepare as prepare_boot,verify as verify_boot
        overrides=dict(overrides or {},**prepare_boot(e))
    if args.hfs_boot:
        from i9100_hfs_boot_copy import prepare as prepare_hfs,verify as verify_hfs,compile_bsd
        hfs_copies=prepare_hfs(e)
        compile_bsd(e,hfs_copies)
    e.cross_compile([name for name in changed+added if name not in hfs_copies],overrides)
    replaced = {}
    def object_name(path):return re.sub(r"^(?:[0-9]{4}-)+","",Path(path).name)
    for name in changed:
        suffix=name+(".cpo" if name.endswith(".cpp") else ".o")
        standard=Path(name).stem+(".cpo" if name.endswith(".cpp") else ".o")
        matches = [i for i, path in enumerate(inputs) if object_name(path) in (suffix,standard)]
        if len(matches) != 1:
            raise RuntimeError("Replacement is missing or ambiguous: " + name)
        item = next(x for x in e.commands if x["source"] == name)
        index = matches[0]
        replaced[name] = dict(previous=inputs[index], current=item["object"])
        captured[index] = str(ROOT / item["object"])
    appended={}
    for name in added:
        suffix=name+(".cpo" if name.endswith(".cpp") else ".o")
        standard=Path(name).stem+(".cpo" if name.endswith(".cpp") else ".o")
        if any(object_name(path) in (suffix,standard) for path in inputs):
            raise RuntimeError("New object is already retained: "+name)
        item=next(x for x in e.commands if x["source"]==name)
        appended[name]=item["object"];captured.append(str(ROOT/item["object"]))
    idle()
    final_inputs, final_snapshot = freeze(captured, e.out / "frozen-final")
    filelist = e.out / "link.filelist"
    filelist.write_text("\n".join(final_inputs) + "\n")
    kernel = e.out / "mach_kernel.sys"
    e.run([*command, "-filelist", str(filelist), *frozen_aux, "-o", str(kernel)],
          "complete-kernel-link", env, cwd=build)
    for item in (snapshot, aux_snapshot, final_snapshot):
        verify(item)
    if digest(flags) != flags_sha or digest(base / "mach_kernel.sys") != expected:
        raise RuntimeError("Reference kernel/link controls changed during assembly")
    if args.sensor_hid_service:verify_sensor_service(e)
    elif manifest.get("sensor_service_copy"):
        e.result["sensor_service_copy"] = manifest["sensor_service_copy"]
    if args.emmc_storage:verify_emmc_service(e)
    elif manifest.get("emmc_service_copy"):e.result["emmc_service_copy"]=manifest["emmc_service_copy"]
    if args.hfs_boot:verify_hfs(e)
    elif manifest.get("hfs_boot_copy"):e.result["hfs_boot_copy"]=manifest["hfs_boot_copy"]
    if args.emmc_boot:verify_boot(e)
    elif manifest.get('emmc_boot_pe_copy'):e.result['emmc_boot_pe_copy']=manifest['emmc_boot_pe_copy']
    prior_peripherals=manifest.get("peripheral_objects")
    if prior_peripherals is None:
        prior_peripherals=manifest.get("retained_peripheral_objects")
        if prior_peripherals is not None:prior_peripherals+=len(manifest.get("appended_objects",{}))
    e.result.update(base=str(base.relative_to(ROOT)), reproduced_sha256=expected,
                    replaced_objects=replaced, appended_objects=appended,total_link_objects=len(captured),
                    kernel_reference=str(reference.relative_to(ROOT)),
                    retained_peripheral_objects=prior_peripherals,
                    peripheral_objects=prior_peripherals+len(added) if prior_peripherals is not None else None,
                    reference_sha256=expected, link_flags_sha256=flags_sha,
                    linked_kernel_sha256=digest(kernel), kernel_bytes=kernel.stat().st_size,
                    frozen_input_manifests=[str(x.relative_to(ROOT)) for x in (snapshot, aux_snapshot, final_snapshot)])
    e.finish([name for name in changed+added if (REPO/"iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform"/name).is_file()],
             "Full retained reference reproduced except LC_UUID; exact bytes verified after normalization; only explicit objects replaced/appended; source-copy hooks retained separately; no live BUILD/loader/hardware changes")
    result = json.loads((e.out / "manifest.json").read_text())
    result["complete_kernel_link"] = True
    (e.out / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
