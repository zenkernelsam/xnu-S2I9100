#!/usr/bin/env python3
"""Actual shared haptic resource/client logic with paired raw wire copies."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
from i9100_power_shared_copy import prepare

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
e = Evidence("power-haptic-offline")
e.wait_idle_seconds = 60
copies = prepare(e)
helpers = [REPO / "port-tests" / name for name in ("i9100_power_haptic_model.h", "i9100_power_test.cpp")]
before = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest() for path in helpers}
e.model_cpp("i9100_power_haptic_test.cpp", ["i9100_power_bus.c", "i9100_power.c"],
            ["i9100_power_shared.cpp"], libraries=("pthread",), source_overrides=copies)
if any(hashlib.sha256((REPO / name).read_bytes()).hexdigest() != sha for name, sha in before.items()):
    raise RuntimeError("Shared haptic model helper changed during verification")
e.result["model_helpers_sha256"] = before
if a.cross_compile:
    e.cross_compile(["i9100_power_shared.cpp", "i9100_power_bus.c", "IOS7I9100Power.cpp", "AppleARMPE.cpp"], copies)
e.finish(["i9100_power_shared.cpp"], "Actual haptic resource leases+paired raw wire; GPIO/chip/owner model, no PWM/motor/electrical validation")
