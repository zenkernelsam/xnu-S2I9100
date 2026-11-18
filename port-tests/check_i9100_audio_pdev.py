#!/usr/bin/env python3
"""Execute the actual pdev through the native user client and PCM/codec models."""
import hashlib
import json
from i9100_offline_build import Evidence, REPO
sources = ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c", "i9100_dma_guard.c",
           "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c", "i9100_gpio_broker.c",
           "i9100_clock_gates.c", "i9100_peripheral_board.c", "i9100_irq_registry.c",
           "i9100_irq_router.c", "i9100_pcm_ring.c", "i9100_audio_user_session.c"]
cpp = ["i9100_audio_native.cpp", "i9100_audio_board_native.cpp", "i9100_audio_worker.cpp", "IOS6I9100AudioPCM.cpp"]
e = Evidence("audio-pdev-offline")
copies = {}
for name in ("i9100-pdev.c", "i9100-pdev.h"):
    original = REPO / "userland/audio" / name
    data = original.read_bytes()
    (e.out / name).write_bytes(data)
    copies[name] = dict(source=str(original.relative_to(REPO)), sha256=hashlib.sha256(data).hexdigest())
e.model_cpp("i9100_audio_pdev_test.cpp", sources + ["i9100-pdev.c"], cpp,
            source_overrides={"i9100-pdev.c": e.out / "i9100-pdev.c"},
            wrapped_functions=("pthread_mutex_init", "pthread_mutex_lock", "pthread_mutex_unlock"))
for item in copies.values():
    if hashlib.sha256((REPO / item["source"]).read_bytes()).hexdigest() != item["sha256"]:
        raise RuntimeError("Pdev source changed during model execution")
e.result["pdev_sources"] = copies
e.finish(sources + cpp, "Actual pdev C/user-client/worker/PCM/codec control; CoreAudio/CF/IOKit/MMIO are fixtures, physical HAL loading and sound remain unverified")
