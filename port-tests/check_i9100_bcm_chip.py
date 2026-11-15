#!/usr/bin/env python3
"""Bounded BCM4330 DMP/F1 topology and RAM diagnostics, never start a radio."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_bcm4330.c","i9100_bcm_chip.c"]
e=Evidence("bcm-chip-offline")
e.model("i9100_bcm_chip_test.c",sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"DMP and F1 wire failure models; no live chip, core reset, firmware/RF or device operations")
