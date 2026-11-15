#!/usr/bin/env python3
"""BCM F2 whole-packet SDIO adapter and failure/generation models, offline."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
e=Evidence("bcm-f2-offline")
e.model("i9100_bcm_f2_test.c",["i9100_bcm_f2.c"])
if a.cross_compile:e.cross_compile(["i9100_bcm_f2.c"])
e.finish(["i9100_bcm_f2.c"],"F2 command/FIFO/padding/atomic publish and fresh-generation models, no radio test")
