#!/usr/bin/env python3
"""Bounded PL330 VM, I2S reset/stop and actual Yamaha bank/mute/gain protocol."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_dma_guard.c","i9100_pl330.c","i9100_i2s.c","i9100_mc1n2.c"]
e=Evidence("audio-transport-offline")
e.model("i9100_audio_transport_test.c",sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"Finite PL330 word-transfer VM and MC1N2/I2S wire/fault contracts, hardware outputs untested")
