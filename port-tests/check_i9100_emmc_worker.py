#!/usr/bin/env python3
"""Run actual eMMC async worker and native/PIO/IDMAC with a thread scheduler boundary."""
import argparse
import hashlib
import os
from i9100_offline_build import Evidence, REPO, ROOT
from i9100_emmc_dma_model import prepare as prepare_dma
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('emmc-worker-offline');e.wait_idle_seconds=60
source=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
if hashlib.sha256(source.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical table changed')
os.environ['I9100_EMMC_HISTORICAL_SOURCE']=str(source)
for name in ('i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c'):(e.out/name).write_bytes((REPO/'port-tests'/name).read_bytes())
c=['i9100_emmc.c','i9100_emmc_idmac.c','i9100_dma_guard.c','i9100_emmc_layout.c','i9100_emmc_writer.c','i9100_emmc_host.c','i9100_emmc_clock.c']
cpp=['i9100_emmc_native.cpp','i9100_emmc_worker.cpp','i9100_dma_native.cpp','i9100_camera_mmio.cpp','IOS6I9100EMMCStorage.cpp','IOS6I9100EMMCRaw.cpp','IOS6I9100EMMCBlockDriver.cpp','i9100_emmc_alias.cpp']
copies=prepare_dma(e)
e.model_cpp('i9100_emmc_worker_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
 source_overrides={'i9100_emmc_native_fixture.c':e.out/'i9100_emmc_native_fixture.c',**copies},
 wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),extra_sdk=REPO/'port-tests/emmc-storage-sdk',libraries=('pthread',))
for scenario in ('hardware-failure','stop-timeout','memory-complete-error','remove-error'):e.run([str(e.out/'models'),scenario],scenario+'-tests')
e.result['nested_fixtures']={n:hashlib.sha256((REPO/'port-tests'/n).read_bytes()).hexdigest()for n in ('i9100_emmc_storage_test.cpp','i9100_emmc_native_test.cpp','i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c')}
e.result['workloop_facade_sha256']=hashlib.sha256((REPO/'port-tests/i9100_emmc_workloop_facade.inc').read_bytes()).hexdigest()
if a.cross_compile:e.cross_compile(c+cpp+['i9100_emmc_boot.cpp','i9100_emmc_publication.cpp'])
e.finish(c+cpp,'Actual async worker/native/MMC/PIO/IDMAC and retained failures; explicit IOKit/thread/MMIO/card facades; no true IRQ,media mount,performance or hardware')
