#!/usr/bin/env python3
"""Offline actual sensor transactions, units/ASA and haptic fail-off contracts."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_sensors.c","i9100_vibrator.c"]
e=Evidence("sensors-haptic-offline")
e.model("i9100_sensors_test.c",sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"K3DH/K3G/AK8975 samples and MAX8997 haptic faults; no hardware/HID test")
