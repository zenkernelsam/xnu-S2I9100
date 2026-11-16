#!/usr/bin/env python3
"""Restricted BCM core-reset preparation and RAM staging integration, offline."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_bcm4330.c","i9100_bcm_chip.c","i9100_bcm_firmware.c","i9100_bcm_core_download.c"]
e=Evidence("bcm-core-download-offline")
e.model("i9100_bcm_core_download_test.c",sources,libraries=["crypto"])
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"Real F1 wire/core reset/fault and RAM staging integration models; no live core actions or CPU/RF activation")
