#!/usr/bin/env python3
"""Run actual native one-shot SDR/pad ownership and retained-error close."""
import argparse
import hashlib
import os
from i9100_offline_build import Evidence,REPO,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('emmc-sdr-native-offline');e.wait_idle_seconds=60
source=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
if hashlib.sha256(source.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical table source changed')
os.environ['I9100_EMMC_HISTORICAL_SOURCE']=str(source)
for n in ('i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c'):(e.out/n).write_bytes((REPO/'port-tests'/n).read_bytes())
c=['i9100_emmc.c','i9100_emmc_layout.c','i9100_emmc_writer.c','i9100_emmc_host.c','i9100_emmc_clock.c']
cpp=['i9100_emmc_native.cpp','i9100_camera_mmio.cpp']
e.model_cpp('i9100_emmc_sdr_native_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
 source_overrides={'i9100_emmc_native_fixture.c':e.out/'i9100_emmc_native_fixture.c'},wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),
 extra_sdk=REPO/'port-tests/emmc-native-sdk',libraries=('pthread',))
for fault in ('switch-error','restore-error','extcsd-corrupt','pad-readback','pad-restore'):e.run([str(e.out/'models'),fault],'fault-'+fault+'-tests')
e.result['nested_fixtures']={n:hashlib.sha256((REPO/'port-tests'/n).read_bytes()).hexdigest()for n in
 ('i9100_emmc_native_test.cpp','i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c')}
if a.cross_compile:e.cross_compile(c+cpp+['IOS7I9100EMMC.cpp','IOS6I9100EMMCRaw.cpp','IOS6I9100EMMCStorage.cpp','IOS6I9100EMMCBlockDriver.cpp','i9100_emmc_alias.cpp','i9100_emmc_publication.cpp'])
e.finish(c+cpp,'Actualnative SDR/core/card/PIO/leases/pad masks/cleanrestore and no-retryretention; IOKit/MMIO/card facade, no real hardware, voltage or filesystem mount')
