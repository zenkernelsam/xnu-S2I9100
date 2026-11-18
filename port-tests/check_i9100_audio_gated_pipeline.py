#!/usr/bin/env python3
"""Actual board/workloop/codec/gated DMA with real instructions and host facades."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
p.add_argument("--wait-idle",type=int,default=60,choices=range(61))
a=p.parse_args()
c=["i9100_audio_power.c","i9100_audss.c","i9100_mc1n2.c","i9100_dma_guard.c","i9100_pl330.c",
   "i9100_i2s.c","i9100_pcm_stream.c","i9100_gpio_broker.c","i9100_clock_gates.c","i9100_peripheral_board.c",
   "i9100_irq_registry.c","i9100_irq_router.c","i9100_pcm_ring.c","i9100_pl330_ring.c"]
cpp=["i9100_audio_native.cpp","i9100_audio_board_native.cpp","i9100_audio_worker.cpp",
     "i9100_pl330_ring_native.cpp","i9100_pl330_ring_mmio.cpp","i9100_audio_gated.cpp"]
e=Evidence("audio-gated-pipeline")
e.wait_idle_seconds=a.wait_idle
e.model_cpp("i9100_audio_gated_pipeline_test.cpp",c,cpp,
 wrapped_functions=("i9100_ring_mmio_read","i9100_ring_mmio_write"))
e.result["nested_fixtures"]={name:hashlib.sha256((REPO/"port-tests"/name).read_bytes()).hexdigest() for name in
 ["i9100_audio_worker_test.cpp","i9100_audio_board_test.cpp","i9100_audio_native_test.cpp","i9100_ring_vm.h"]}
if a.cross_compile:e.cross_compile(c+cpp)
e.finish(c+cpp,"Actual board/workloop/I2S/codec/ring controller path; physical registers/allocations/IOKit are explicit host facades, no sound/device verification")
