#!/usr/bin/env python3
"""Actual native transport owner/lifetime logic with a host IOKit facade."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c","i9100_peripheral_board.c",
         "i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c"]
e=Evidence("native-transports-offline")
helper=REPO/"port-tests/i9100_native_parent_model.h"
before=hashlib.sha256(helper.read_bytes()).hexdigest()
e.model_cpp("i9100_native_transport_test.cpp",sources,[])
if hashlib.sha256(helper.read_bytes()).hexdigest()!=before:raise RuntimeError("Native fixture changed during verification")
e.result["native_parent_fixture_sha256"]=before
if a.cross_compile:e.cross_compile(sources+["i9100_peripheral_native.cpp"])
e.finish(sources+["i9100_peripheral_native.cpp"],"Production native implementation compiled in host model; epoch/close/failure ownership, no actual F1 enumeration or device IO")
