#!/usr/bin/env python3
"""FC18 actual command/Complete/host-change protocol; explicit host callbacks."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('bt-baud-offline');e.wait_idle_seconds=60
sources=['i9100_bluetooth_h4.c','i9100_bt_boot.c']
e.model('i9100_bt_baud_test.c',sources,libraries=['crypto'])
e.result['nested_fixtures']={'i9100_bt_boot_test.c':hashlib.sha256((REPO/'port-tests/i9100_bt_boot_test.c').read_bytes()).hexdigest()}
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,'Actual H4/FC18 transition and new-speed identity with host/time/controller callbacks explicitly modeled; not actual UART/electrical acceptance')
