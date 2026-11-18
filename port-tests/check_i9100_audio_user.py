#!/usr/bin/env python3
"""Bounded user ABI and actual IOUserClient/worker/PCM with a host IOKit facade."""
import argparse
from i9100_offline_build import Evidence
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
sources = ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c", "i9100_dma_guard.c",
           "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c", "i9100_gpio_broker.c",
           "i9100_clock_gates.c", "i9100_peripheral_board.c", "i9100_irq_registry.c",
           "i9100_irq_router.c", "i9100_pcm_ring.c", "i9100_audio_user_session.c"]
cpp = ["i9100_audio_native.cpp", "i9100_audio_board_native.cpp", "i9100_audio_worker.cpp", "IOS6I9100AudioPCM.cpp"]
e = Evidence("audio-user-offline")
e.model("i9100_audio_user_test.c", ["i9100_audio_user_session.c"])
e.model_cpp("i9100_audio_user_native_test.cpp", sources, cpp)
if a.cross_compile:
    e.cross_compile(sources + cpp)
e.finish(sources + cpp, "Real user-client and PCM/codec control with modeled IOKit/MMIO; no HAL loading, physical clocks or sound validation")
