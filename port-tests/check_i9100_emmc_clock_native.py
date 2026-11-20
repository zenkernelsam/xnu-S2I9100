#!/usr/bin/env python3
"""Execute actual IOKit read-only eMMC clock observer, single lease and allocation faults."""
import argparse
import hashlib
import os
from i9100_offline_build import Evidence,REPO,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('emmc-clock-native-offline');e.wait_idle_seconds=60
for name in ('i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c'):
    (e.out/name).write_bytes((REPO/'port-tests'/name).read_bytes())
source=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
if hashlib.sha256(source.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical card fixture source drift')
os.environ['I9100_EMMC_HISTORICAL_SOURCE']=str(source)
c=['i9100_emmc_clock.c','i9100_emmc_host.c','i9100_emmc.c','i9100_emmc_layout.c','i9100_emmc_writer.c']
cpp=['i9100_emmc_clock_native.cpp','i9100_camera_mmio.cpp']
e.model_cpp('i9100_emmc_clock_native_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
 source_overrides={'i9100_emmc_native_fixture.c':e.out/'i9100_emmc_native_fixture.c'},
 extra_sdk=REPO/'port-tests/emmc-storage-sdk',wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),libraries=('pthread',))
hook_scenarios=('conflict-data','conflict-legacy','conflict-root','conflict-rootdev','bad-root-clock','busy-hook')
for scenario in hook_scenarios:e.run([str(e.out/'models'),scenario],'hook-'+scenario+'-tests')
e.result['hook_scenarios']=list(hook_scenarios)
e.result['nested_fixtures']={name:hashlib.sha256((REPO/'port-tests'/name).read_bytes()).hexdigest()for name in
 ('i9100_emmc_native_test.cpp','i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c')}
if a.cross_compile:e.cross_compile(c+cpp)
e.finish(c+cpp,'Actual read-only observer/native128KiB CMU maps/hostlease/failed resource retention; explicit IOkit/MMIO/card/time facade, no device or MMC writes')
