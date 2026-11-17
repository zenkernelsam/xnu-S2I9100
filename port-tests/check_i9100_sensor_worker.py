#!/usr/bin/env python3
"""Real sensor worker/native/controller/core with a modeled hardware boundary."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
p.add_argument("--wait-idle", type=int, choices=range(61), default=0, metavar="0..60")
a = p.parse_args()
sources = ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c",
           "i9100_peripheral_board.c", "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c",
           "i9100_sdio.c", "i9100_irq_cascade.c", "i9100_sensors.c"]
helpers = [REPO / "port-tests" / name for name in
           ("i9100_native_parent_model.h", "i9100_native_i2c_model.h", "i9100_i2c_register_model.h")]
before = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest() for path in helpers}
e = Evidence("sensor-worker-offline")
e.wait_idle_seconds = a.wait_idle
e.model_cpp("i9100_sensor_worker_test.cpp", sources, ["i9100_sensor_worker.cpp"],
            wrapped_functions=("i9100_i2c_samsung_init",))
if any(hashlib.sha256((REPO / name).read_bytes()).hexdigest() != sha for name, sha in before.items()):
    raise RuntimeError("Native sensor model helper changed during verification")
e.result["model_helpers_sha256"] = before
if a.cross_compile:
    e.cross_compile(sources + ["i9100_peripheral_native.cpp", "i9100_sensor_worker.cpp"])
e.finish(sources + ["i9100_peripheral_native.cpp", "i9100_sensor_worker.cpp"],
         "Actual worker/native I2C/controller/sensor pipeline; register/slave/IOKit stand-ins, no physical or CoreMotion proof")
