#!/usr/bin/env python3
"""Run actual DPB decoder/core with explicit firmware and NC memory boundaries."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cross-compile',action='store_true')
a=p.parse_args()
sources=['i9100_mfc.c','i9100_mfc_decode.c','i9100_mfc_layout.c','i9100_dma_guard.c']
e=Evidence('mfc-decode-offline')
e.wait_idle_seconds=60
e.model('i9100_mfc_decode_test.c',sources,libraries=('crypto',))
e.result['nested_fixtures']={'i9100_mfc_tile_model.h':hashlib.sha256((REPO/'port-tests/i9100_mfc_tile_model.h').read_bytes()).hexdigest()}
if a.cross_compile:e.cross_compile(sources+['i9100_mfc_native.cpp','i9100_dma_native.cpp'])
e.finish(sources+['i9100_mfc_native.cpp','i9100_dma_native.cpp'],
 'Actual core/DPB/leases/CPB and NV12MT conversion; independent tile fixture and explicit firmware/register/NC facades; no codec execution, decoded image, physical cache or supply proof')
