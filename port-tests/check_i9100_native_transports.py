#!/usr/bin/env python3
"""Actual native transport owner/lifetime logic with a host IOKit facade."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c","i9100_peripheral_board.c",
         "i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c"]
e=Evidence("native-transports-offline")
e.model_cpp("i9100_native_transport_test.cpp",sources,[])
if a.cross_compile:e.cross_compile(sources+["i9100_peripheral_native.cpp"])
e.finish(sources+["i9100_peripheral_native.cpp"],"Production native implementation compiled in host model; epoch/close/failure ownership, no actual F1 enumeration or device IO")
