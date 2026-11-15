#!/usr/bin/env python3
"""U1 board, Samsung I2C, EINT/combiner, AUDSS and native DMA bridge checks."""
import argparse
from i9100_offline_build import Evidence

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
sources = ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_peripheral_board.c",
           "i9100_i2c_samsung.c", "i9100_irq_cascade.c", "i9100_audss.c", "i9100_dma_guard.c"]
e = Evidence("peripheral-platform-offline")
e.model("i9100_peripheral_platform_test.c", sources)
if a.cross_compile:
    e.cross_compile(sources + ["i9100_irq_registry.c","i9100_dma_native.cpp","i9100_peripheral_native.cpp"])
e.finish(sources + ["i9100_irq_registry.c","i9100_dma_native.cpp","i9100_peripheral_native.cpp"],
         "Offline register/clock fault models and real ARM/XNU compilation; no live pin/DMA test")
