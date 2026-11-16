#!/usr/bin/env python3
"""WLAN event bytes/association policy through actual BCM/CDC/F2 drivers, offline."""
import argparse
from i9100_offline_build import Evidence

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
sources = ["i9100_bcm4330.c", "i9100_bcm_f2.c", "i9100_wlan_control.c"]
e = Evidence("wlan-control-offline")
e.model("i9100_wlan_control_test.c", sources)
if a.cross_compile:
    e.cross_compile(sources)
e.finish(sources, "Actual F2/CDC/event/owner/association logic; SDIO card and firmware producer are stand-ins, no radio or iOS network service")
