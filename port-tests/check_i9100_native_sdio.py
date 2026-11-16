#!/usr/bin/env python3
"""Production native SDIO + card driver with only a modeled register boundary."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
p.add_argument("--wait-idle", type=int, choices=range(61), default=0, metavar="0..60")
args = p.parse_args()
sources = ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c",
           "i9100_peripheral_board.c", "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c",
           "i9100_sdio.c", "i9100_irq_cascade.c"]
helpers = [REPO / "port-tests" / name for name in
           ("i9100_native_parent_model.h", "i9100_sdio_register_model.h")]
before = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest()
          for path in helpers}
e = Evidence("native-sdio-offline")
e.wait_idle_seconds = args.wait_idle
e.model_cpp("i9100_native_sdio_success_test.cpp", sources, [],
            wrapped_functions=("i9100_sdio_init",))
if any(hashlib.sha256((REPO / name).read_bytes()).hexdigest() != sha
       for name, sha in before.items()):
    raise RuntimeError("Native/register fixture changed during verification")
e.result["model_helpers_sha256"] = before
e.result["hardware_boundary"] = "SDHCI registers/card and IOKit facade are stand-ins"
if args.cross_compile:
    e.cross_compile(sources + ["i9100_peripheral_native.cpp"])
e.finish(sources + ["i9100_peripheral_native.cpp"],
         "Production native orchestration + production SDIO card driver; no real MMIO, phone, supply or FW/RF proof")
