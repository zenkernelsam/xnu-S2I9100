#!/usr/bin/env python3
"""Real IOKit audio worker with serial gate/IRQ/event lifecycle facade."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
sources = ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c", "i9100_dma_guard.c",
           "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c", "i9100_gpio_broker.c",
           "i9100_clock_gates.c", "i9100_peripheral_board.c", "i9100_irq_registry.c", "i9100_irq_router.c", "i9100_pcm_ring.c"]
cpp = ["i9100_audio_native.cpp", "i9100_audio_board_native.cpp", "i9100_audio_worker.cpp"]
e = Evidence("audio-worker-offline")
e.model_cpp("i9100_audio_worker_test.cpp", sources, cpp)
e.result["model_includes"] = {name: hashlib.sha256((REPO / "port-tests" / name).read_bytes()).hexdigest()
                            for name in ["i9100_audio_native_test.cpp", "i9100_audio_board_test.cpp"]}
if a.cross_compile:
    e.cross_compile(sources + cpp)
e.finish(sources + cpp, "Real work-loop API/IRQ lifecycle with host facade; actual IRQ routing/timing/FIFO/audio untested")
