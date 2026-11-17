#!/usr/bin/env python3
"""Real haptic worker/native/PWM/Power chain with modeled hardware/IOKit."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
from i9100_power_shared_copy import prepare

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
e = Evidence("haptic-worker-offline")
e.wait_idle_seconds = 60
copies = prepare(e)
sources = ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c",
           "i9100_irq_cascade.c", "i9100_peripheral_board.c", "i9100_i2c_samsung.c",
           "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_pwm_haptic.c",
           "i9100_vibrator.c", "i9100_power_bus.c", "i9100_power.c"]
helpers = [REPO / "port-tests" / name for name in
           ("i9100_haptic_native_model.h", "i9100_power_haptic_model.h", "i9100_power_test.cpp")]
before = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest() for path in helpers}
e.model_cpp("i9100_haptic_worker_test.cpp", sources,
            ["i9100_power_shared.cpp", "i9100_haptic_worker.cpp"], libraries=("pthread",), source_overrides=copies)
if any(hashlib.sha256((REPO / name).read_bytes()).hexdigest() != sha for name, sha in before.items()):
    raise RuntimeError("Haptic worker model helper changed during verification")
e.result["model_helpers_sha256"] = before
if a.cross_compile:
    e.cross_compile(sources + ["i9100_peripheral_native.cpp", "i9100_power_shared.cpp", "i9100_haptic_worker.cpp", "IOS7I9100Power.cpp", "AppleARMPE.cpp"], copies)
e.finish(sources + ["i9100_peripheral_native.cpp", "i9100_power_shared.cpp", "i9100_haptic_worker.cpp"],
         "Actual native haptic workloop/Power/PWM/vibrator; MMIO/slave/IOKit stand-ins, no physical motor")
