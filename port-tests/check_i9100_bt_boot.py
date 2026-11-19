#!/usr/bin/env python3
"""Broadcom HCD/bootstrap state machine with actual H4 and bounded controller model."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
sources=['i9100_bluetooth_h4.c','i9100_bt_boot.c']
e=Evidence('bt-boot-offline');e.wait_idle_seconds=60
e.model('i9100_bt_boot_test.c',sources,libraries=['crypto'])
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,'Actual H4/BCM patchram state machine; controller/ROM replies, HCD assets and clock are explicit models; no native/physical/iOS HCI validation')
