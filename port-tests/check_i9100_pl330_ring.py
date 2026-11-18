#!/usr/bin/env python3
"""Verify actual ring bytecode, event semantics and guarded period transitions."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_dma_guard.c","i9100_i2s.c","i9100_pl330_ring.c"]
e=Evidence("pl330-ring-offline")
e.model("i9100_pl330_ring_test.c",sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"Generated PL330 ring instructions, WFE per-buffer fences and no event coalescing with host DMA/register VM; native I2S/workloop/physical timing unverified")
