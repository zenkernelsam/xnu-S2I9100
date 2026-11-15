#!/usr/bin/env python3
"""CPU PCM staging ring contracts and ARM compilation, no audio/DMA output."""
import argparse
from i9100_offline_build import Evidence
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
e = Evidence("pcm-ring-offline")
sources = ["i9100_pcm_ring.c"]
e.model("i9100_pcm_ring_test.c", sources)
if a.cross_compile:
    e.cross_compile(sources)
e.finish(sources, "CPU-only staging ring; native PCM gain and hardware timing are separate contracts")
