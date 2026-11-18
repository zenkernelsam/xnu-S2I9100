#!/usr/bin/env python3
"""Actual native ring adapter and bytecode with explicit MMIO/allocation facades."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
c=["i9100_dma_guard.c","i9100_i2s.c","i9100_clock_gates.c","i9100_peripheral_board.c","i9100_pl330_ring.c"]
cpp=["i9100_pl330_ring_native.cpp","i9100_pl330_ring_mmio.cpp"]
e=Evidence("pl330-ring-native-offline")
e.model_cpp("i9100_pl330_ring_native_test.cpp",c,cpp,
 wrapped_functions=("i9100_ring_mmio_read","i9100_ring_mmio_write"))
e.result["vm_fixture_sha256"]=hashlib.sha256((REPO/"port-tests/i9100_pl330_ring_test.c").read_bytes()).hexdigest()
if a.cross_compile:e.cross_compile(c+cpp)
e.finish(c+cpp,"Actual native ring control/clock lifecycle and PL330 VM; MMIO/memory/consumer are explicit host facades; no real PTE/cache/clock/audio verification")
