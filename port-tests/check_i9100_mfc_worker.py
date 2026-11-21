#!/usr/bin/env python3
"""Actual MFC IRQ126 lease, native decoder, deferred IOWorkLoop worker offline."""
import argparse,hashlib
from i9100_offline_build import Evidence,REPO
from i9100_mfc_dma_model import prepare
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cross-compile',action='store_true')
a=p.parse_args()
c=['i9100_mfc_power.c','i9100_mfc_host.c','i9100_mfc.c','i9100_mfc_decode.c','i9100_mfc_layout.c','i9100_dma_guard.c','i9100_irq_registry.c','i9100_mfc_irq.c']
cpp=['i9100_mfc_board_native.cpp','i9100_mfc_native.cpp','i9100_mfc_worker.cpp','i9100_dma_native.cpp','i9100_camera_mmio.cpp']
e=Evidence('mfc-worker-offline');e.wait_idle_seconds=60
overrides=prepare(e)
e.model_cpp('i9100_mfc_worker_test.cpp',c,cpp,libraries=('crypto','pthread'),source_overrides=overrides,
 wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),extra_sdk=REPO/'port-tests/dma-native-sdk',
 extra_defines=('I9100_MFC_WORKER_FACADE=1',))
e.result['actual_mfc_deferred_worker']=True
e.result['worker_facade_sha256']=hashlib.sha256((REPO/'port-tests/i9100_mfc_worker_facade.h').read_bytes()).hexdigest()
e.result['model_preparer_sha256']=hashlib.sha256((REPO/'port-tests/i9100_mfc_dma_model.py').read_bytes()).hexdigest()
if a.cross_compile:e.cross_compile(c+cpp)
e.finish(c+cpp,'Actual board->native->IRQ126 masked lease->notify-only ISR->IOWorkLoop deferred completion, bounded FIFO/backpressure/EOS drain/lease and retained cleanup; explicit GIC/firmware/timer/thread/IOKit facades; no hardware IRQ delivery, codec or supply proof')
