#!/usr/bin/env python3
"""BCM halted RAM image integrity/lifecycle models with real host SHA256."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_bcm4330.c","i9100_bcm_chip.c","i9100_bcm_firmware.c"]
e=Evidence("bcm-firmware-offline")
e.model("i9100_bcm_firmware_test.c",sources,libraries=["crypto"])
if a.cross_compile:e.cross_compile(sources+["i9100_bcm_firmware_crypto.c"])
e.finish(sources+["i9100_bcm_firmware_crypto.c"],"OpenSSL host integrity and real F1 wire/lifecycle models; kernel crypto adapter compiled only; no live RAM/FW/RF")
