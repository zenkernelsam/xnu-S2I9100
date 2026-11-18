#!/usr/bin/env python3
"""Actual original pdev ABI through the new gated kernel path and hardware VM."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--wait-idle",type=int,default=60,choices=range(61))
a=p.parse_args()
c=["i9100_audio_power.c","i9100_audss.c","i9100_mc1n2.c","i9100_dma_guard.c","i9100_pl330.c",
   "i9100_i2s.c","i9100_pcm_stream.c","i9100_gpio_broker.c","i9100_clock_gates.c","i9100_peripheral_board.c",
   "i9100_irq_registry.c","i9100_irq_router.c","i9100_pcm_ring.c","i9100_pl330_ring.c","i9100_audio_user_session.c"]
cpp=["i9100_audio_native.cpp","i9100_audio_board_native.cpp","i9100_audio_worker.cpp","i9100_pl330_ring_native.cpp",
     "i9100_pl330_ring_mmio.cpp","i9100_audio_gated.cpp","IOS6I9100AudioPCM.cpp"]
e=Evidence("audio-gated-pdev")
e.wait_idle_seconds=a.wait_idle
captured={}
for name in ("i9100-pdev.c","i9100-pdev.h"):
 data=(REPO/"userland/audio"/name).read_bytes();(e.out/name).write_bytes(data);captured[name]=hashlib.sha256(data).hexdigest()
e.model_cpp("i9100_audio_gated_pdev_test.cpp",c+["i9100-pdev.c"],cpp,
 source_overrides={"i9100-pdev.c":e.out/"i9100-pdev.c"},
 wrapped_functions=("i9100_ring_mmio_read","i9100_ring_mmio_write","pthread_mutex_init","pthread_mutex_lock","pthread_mutex_unlock"))
for name,sha in captured.items():
 if hashlib.sha256((REPO/"userland/audio"/name).read_bytes()).hexdigest()!=sha:raise RuntimeError("Pdev input changed")
e.result["pdev_sources"]=captured
e.finish(c+cpp,"Actual pdev/gated user-client/board/workloop/codec/I2S/PL330 bytecode; explicit CoreAudio/IOKit/MMIO fixtures, physical loading/sound unverified")
