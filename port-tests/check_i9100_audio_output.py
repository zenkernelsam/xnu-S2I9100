#!/usr/bin/env python3
"""Owned MC1N2 manual output sequencing, actual PCM qualification and cleanup faults."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
p.add_argument("--wait-idle", type=int, default=60, choices=range(61))
a = p.parse_args()
sources = ["i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_mc1n2.c", "i9100_pcm_stream.c"]
cpp = ["i9100_audio_native.cpp", "i9100_audio_board_native.cpp", "i9100_audio_worker.cpp"]
e = Evidence("audio-output-offline")
e.wait_idle_seconds = a.wait_idle
e.model("i9100_audio_output_test.c", sources)
e.result["fixture_sha256"] = hashlib.sha256((REPO / "port-tests/i9100_audio_transport_test.c").read_bytes()).hexdigest()
if a.cross_compile:
    e.cross_compile(sources + cpp)
e.finish(sources + (cpp if a.cross_compile else []), "Owned output/real PCM protocol model only; board MMIO, clocks, DAC polarity and acoustics unmeasured")
