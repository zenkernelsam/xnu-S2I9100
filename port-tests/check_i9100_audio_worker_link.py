#!/usr/bin/env python3
"""Reproduce a verified full kernel, then replace only the current audio board/worker.

Keeps all prior peripheral and paired Power objects; never edits shared BUILD,
live integration patches, loader packages or hardware. Not a production build.
"""
import argparse
import hashlib
import json
import os
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
    p.add_argument("--wait-idle", type=int, default=60, choices=range(61))
    args = p.parse_args()
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
    aux = previous_inputs[-1]
    if len(aux) != 2 or any(not x["frozen"].endswith(suffix) for x, suffix in zip(aux, ("version.o", "lastkernelconstructor.o"))):
        raise RuntimeError("Auxiliary constructor/version inputs are ambiguous")
    reference = (ROOT / manifest["kernel_reference"]).resolve()
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
    e = Evidence("audio-worker-link")
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
    e.cross_compile(list(CHANGED))
    replaced = {}
    for name in CHANGED:
        matches = [i for i, path in enumerate(inputs) if Path(path).name == name + ".cpo"]
        if len(matches) != 1:
            raise RuntimeError("Replacement is missing or ambiguous: " + name)
        item = next(x for x in e.commands if x["source"] == name)
        index = matches[0]
        replaced[name] = dict(previous=inputs[index], current=item["object"])
        captured[index] = str(ROOT / item["object"])
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
    e.result.update(base=str(base.relative_to(ROOT)), reproduced_sha256=expected,
                    replaced_objects=replaced, total_link_objects=len(inputs),
                    retained_peripheral_objects=manifest.get("peripheral_objects"),
                    reference_sha256=expected, link_flags_sha256=flags_sha,
                    linked_kernel_sha256=digest(kernel), kernel_bytes=kernel.stat().st_size,
                    frozen_input_manifests=[str(x.relative_to(ROOT)) for x in (snapshot, aux_snapshot, final_snapshot)])
    e.finish(list(CHANGED), "Full reference reproduced except LC_UUID; exact bytes verified after UUID normalization; two current audio objects replaced; no live main/loader/hardware changes")
    result = json.loads((e.out / "manifest.json").read_text())
    result["complete_kernel_link"] = True
    (e.out / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
