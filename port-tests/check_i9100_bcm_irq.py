#!/usr/bin/env python3
"""Actual native/EINT/GIC/BCM IRQ adapter with register and worker stand-ins."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c","i9100_irq_cascade.c",
         "i9100_irq_router.c","i9100_peripheral_board.c","i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c"]
e=Evidence("bcm-irq-offline")
e.wait_idle_seconds=60
e.model_cpp("i9100_bcm_irq_test.cpp",sources,["i9100_bcm_irq.cpp"])
if a.cross_compile:e.cross_compile(sources+["i9100_peripheral_native.cpp","i9100_bcm_worker.cpp","i9100_bcm_irq.cpp"])
e.finish(sources+["i9100_peripheral_native.cpp","i9100_bcm_worker.cpp","i9100_bcm_irq.cpp"],
         "Real native/core/router/IRQ adapter; register writes and worker hooks are stand-ins, cached SDIO hold, no actual IRQ/native F1/electrical test")
