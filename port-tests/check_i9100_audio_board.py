#!/usr/bin/env python3
"""Real board power/codec/PCM adapters with a host IOKit and protected-page model."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
sources = ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c", "i9100_dma_guard.c",
           "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c", "i9100_gpio_broker.c",
           "i9100_clock_gates.c", "i9100_peripheral_board.c"]
cpp = ["i9100_audio_native.cpp", "i9100_audio_board_native.cpp"]
e = Evidence("audio-board-offline")
e.model_cpp("i9100_audio_board_test.cpp", sources, cpp)
e.result["model_includes"] = {"i9100_audio_native_test.cpp": hashlib.sha256((REPO / "port-tests/i9100_audio_native_test.cpp").read_bytes()).hexdigest()}
if a.cross_compile:
    e.cross_compile(sources + cpp)
e.finish(sources + cpp, "Real board adapter with host IOKit; no physical PMU/rail/MCLK/BCLK or audible-output validation")
