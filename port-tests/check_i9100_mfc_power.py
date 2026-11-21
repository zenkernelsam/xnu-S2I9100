#!/usr/bin/env python3
"""Execute real MFC local power sequencing and exact shared host ownership offline."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cross-compile',action='store_true')
a=p.parse_args()
sources=['i9100_mfc_power.c','i9100_mfc_host.c']
e=Evidence('mfc-power-offline');e.wait_idle_seconds=60
e.model('i9100_mfc_power_test.c',sources)
if a.cross_compile:e.cross_compile(sources+['i9100_mfc_board_native.cpp','i9100_mfc_native.cpp'])
e.finish(sources+['i9100_mfc_board_native.cpp','i9100_mfc_native.cpp'],
 'Actual MFC root/child resource ownership and local power/leaf-clock sequence; explicit PMU/CMU/IRQ/MMU/time model, no physical rail/clock/firmware proof')
