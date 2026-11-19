#!/usr/bin/env python3
"""Actual H4 host reset transaction with original-shaped controller facades."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('bt-host-reset-offline');e.wait_idle_seconds=60
sources=['i9100_bluetooth_h4.c','i9100_bt_boot.c']
e.model('i9100_bt_host_reset_test.c',sources,libraries=['crypto'])
e.result['nested_fixtures']={'i9100_bt_boot_test.c':hashlib.sha256((REPO/'port-tests/i9100_bt_boot_test.c').read_bytes()).hexdigest()}
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,'Actual H4/reset/identity/epoch code, explicit controller/version/HCD/time facades; no actual radio or complete BTServer')
