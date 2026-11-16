#!/usr/bin/env python3
"""U1 GPIO sequencing and radio supply/pin lifetime models, offline only."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_gpio_broker.c","i9100_peripheral_board.c","i9100_radio_power.c"]
e=Evidence("radio-power-offline")
e.model("i9100_radio_power_test.c",sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"GPIO/order/time/supply/hold/failure models only; real rails32k/electrical/radio untested")
