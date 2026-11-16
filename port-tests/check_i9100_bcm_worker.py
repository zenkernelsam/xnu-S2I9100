#!/usr/bin/env python3
"""Real IOKit receive worker against upper-driver wire models, offline."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
p.add_argument("--arm-only", action="store_true", help="Compiler evidence only; no model claim")
a = p.parse_args()
sources = ["i9100_bcm4330.c", "i9100_bcm_chip.c", "i9100_bcm_core_download.c", "i9100_bcm_firmware.c",
           "i9100_bcm_f2.c", "i9100_bcm_boot.c", "i9100_wlan_control.c", "i9100_bcm_pump.c"]
cpp = ["i9100_bcm_worker.cpp"]
e = Evidence("bcm-worker-offline")
e.wait_idle_seconds = 60
helpers = [REPO / "port-tests/i9100_bcm_runtime_model.h", REPO / "port-tests/i9100_bcm_wire_model.h"]
before = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest() for path in helpers}
if not a.arm_only:
    e.model_cpp("i9100_bcm_worker_test.cpp", sources, cpp, libraries=["crypto"])
if a.cross_compile or a.arm_only:
    e.cross_compile(cpp if a.arm_only else sources + cpp)
if any(hashlib.sha256((REPO / name).read_bytes()).hexdigest() != digest for name, digest in before.items()):
    raise RuntimeError("Runtime fixture changed during verification")
e.result["runtime_fixture_sha256"] = before
e.finish(cpp if a.arm_only else sources + cpp,
         "ARM/XNU compiler only, host model pending" if a.arm_only else
         "Actual IOKit worker/pump/F1/FW/F2/CDC/controller; IOKit/native-hold/card/FW producers are stand-ins, no physical IRQ/iOS network proof")
