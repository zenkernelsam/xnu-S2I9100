#!/usr/bin/env python3
"""Compile every offline peripheral object and optionally link a complete kernel.

Retains the active reference object's overrides and writes only to this out.
No generated BUILD rules, loader package, USB, serial, flashing or cleanup.
"""
import argparse
import hashlib
import json
import os
import shlex
import time
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM, idle

C_SOURCES = [
    "i9100_gpio_broker.c", "i9100_i2c_gpio.c", "i9100_clock_gates.c",
    "i9100_irq_registry.c", "i9100_dma_guard.c", "i9100_peripheral_board.c",
    "i9100_i2c_samsung.c", "i9100_irq_cascade.c", "i9100_audss.c",
    "i9100_sensors.c", "i9100_vibrator.c", "i9100_pl330.c", "i9100_i2s.c",
    "i9100_mc1n2.c", "i9100_sdio.c", "i9100_bcm4330.c", "i9100_bluetooth_h4.c",
    "i9100_camera_protocol.c", "i9100_mfc.c", "i9100_video_layout.c",
    "i9100_sysmmu.c", "i9100_modem_sipc4.c", "i9100_emmc.c", "i9100_emmc_layout.c",
    "i9100_uart_bluetooth.c", "i9100_pwm_haptic.c",
    "i9100_bcm_f2.c",
]
CPP_SOURCES = ["i9100_dma_native.cpp", "i9100_peripheral_native.cpp",
               "IOS7I9100EMMC.cpp", "IOS7I9100GPT.cpp"]
TESTS = [
    ("i9100_peripheral_bus_test.c", C_SOURCES[:5]),
    ("i9100_peripheral_platform_test.c", ["i9100_gpio_broker.c", "i9100_clock_gates.c",
        "i9100_peripheral_board.c", "i9100_i2c_samsung.c", "i9100_irq_cascade.c", "i9100_audss.c", "i9100_dma_guard.c"]),
    ("i9100_sensors_test.c", ["i9100_sensors.c", "i9100_vibrator.c"]),
    ("i9100_audio_transport_test.c", ["i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_mc1n2.c"]),
    ("i9100_wifi_bluetooth_test.c", ["i9100_sdio.c", "i9100_bcm4330.c", "i9100_bluetooth_h4.c"]),
    ("i9100_camera_mfc_test.c", ["i9100_camera_protocol.c", "i9100_mfc.c", "i9100_video_layout.c", "i9100_sysmmu.c", "i9100_dma_guard.c"]),
    ("i9100_modem_test.c", ["i9100_modem_sipc4.c"]),
    ("i9100_uart_pwm_test.c", ["i9100_uart_bluetooth.c", "i9100_pwm_haptic.c"]),
    ("i9100_bcm_f2_test.c", ["i9100_bcm_f2.c"]),
]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--link", action="store_true")
    p.add_argument("--models", action="store_true")
    p.add_argument("--reference", type=Path, help="successful retained control-* kernel directory")
    p.add_argument("--wait-idle", type=int, default=0, choices=range(0, 61), metavar="0..60")
    args = p.parse_args()
    e = Evidence("peripheral-all-offline")
    deadline = time.monotonic() + args.wait_idle
    while True:
        try:
            idle()
            break
        except RuntimeError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(2)
    if args.models:
        for test, sources in TESTS:
            folder = e.out / Path(test).stem
            folder.mkdir()
            previous = e.out
            e.out = folder
            e.model(test, sources)
            e.out = previous
        e.result["model_suites"] = [name for name, _ in TESTS]
    e.cross_compile(C_SOURCES + CPP_SOURCES)
    if args.link:
        if args.reference:
            reference = args.reference.resolve()
        else:
            candidates = [folder for folder in sorted((REPO / "out/automation").glob("control-*"), reverse=True)
                          if (folder / "mach_kernel").exists() and (folder / "link.filelist").exists()]
            if not candidates:
                raise RuntimeError("No complete reference kernel")
            reference = candidates[0]
        if not reference.is_relative_to(REPO / "out/automation"):
            raise RuntimeError("Reference must remain under the retained repo out/automation")
        reference_hash = hashlib.sha256((reference / "mach_kernel").read_bytes()).hexdigest()
        base_list = (reference / "link.filelist").read_text().splitlines()
        if not base_list or any(not Path(name).is_file() for name in base_list):
            raise RuntimeError("Incomplete reference objects; do not silently replace their overrides")
        extra = [str(ROOT / item["object"]) for item in e.commands]
        known = set(base_list)
        if known.intersection(extra):
            raise RuntimeError("New objects duplicate reference entries")
        filelist = e.out / "link.filelist"
        filelist.write_text("\n".join(base_list + extra) + "\n")
        build = REPO / "BUILD/obj/RELEASE_ARM_EXYNOS4210_I9100"
        command = shlex.split((reference / "link-flags.txt").read_text().strip().splitlines()[-1])
        command.extend(["-filelist", str(filelist), str(build / "version.o"),
                        str(build / "lastkernelconstructor.o"), "-o", str(e.out / "mach_kernel.sys")])
        env = dict(os.environ, TC=str(ROOT / "assets/i9100-toolchain"), SDKROOT="/",
                   LD64_WRAPPER=str(ROOT / "assets/i9100-toolchain/bin/ld64"),
                   SRCROOT=str(REPO), OBJROOT=str(REPO / "BUILD/obj"),
                   SYMROOT=str(REPO / "BUILD/sym"), TARGET=str(build))
        env["PATH"] = str(ROOT / "assets/i9100-toolchain/bin") + ":" + env["PATH"]
        e.run(command, "complete-kernel-link", env, cwd=build)
        if hashlib.sha256((reference / "mach_kernel").read_bytes()).hexdigest() != reference_hash:
            raise RuntimeError("Reference changed during linking; result cannot be called a fixed comparison")
        e.result.update(kernel_reference=str(reference.relative_to(ROOT)),
                        reference_sha256=reference_hash, reference_objects=len(base_list),
                        new_objects=len(extra), linked_kernel_sha256=hashlib.sha256((e.out / "mach_kernel.sys").read_bytes()).hexdigest())
    e.finish(C_SOURCES + CPP_SOURCES, "Offline default-OFF peripheral objects; no loader package or live hardware")
    if args.link:
        result = json.loads((e.out / "manifest.json").read_text())
        result["complete_kernel_link"] = True
        (e.out / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
