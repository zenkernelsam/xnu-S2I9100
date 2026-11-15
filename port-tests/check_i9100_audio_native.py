#!/usr/bin/env python3
"""Actual IOKit audio bridge with host-only allocation/ownership fault facade."""
import argparse
from i9100_offline_build import Evidence
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
sources = ["i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c",
           "i9100_mc1n2.c", "i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_peripheral_board.c"]
cpp = ["i9100_audio_native.cpp"]
e = Evidence("audio-native-offline")
e.model_cpp("i9100_audio_native_test.cpp", sources, cpp)
if a.cross_compile:
    e.cross_compile(sources + cpp)
e.finish(sources + cpp, "Native audio resource lifecycle in host IOKit facade; no real codec/DMA/cache/output test")
