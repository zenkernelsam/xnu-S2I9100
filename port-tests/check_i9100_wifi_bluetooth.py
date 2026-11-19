#!/usr/bin/env python3
"""SDIO host/card, BCM backplane/SDPCM and Bluetooth H4/HCD offline checks."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_sdio.c","i9100_bcm4330.c","i9100_bluetooth_h4.c"]
e=Evidence("wifi-bt-offline")
e.wait_idle_seconds=60
e.model("i9100_wifi_bluetooth_test.c",sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"PIO SDIO/CIS/CCCR/FBR, BCM SDPCM/BDC/CDC and H4/HCD protocol faults; no radio/firmware test")
