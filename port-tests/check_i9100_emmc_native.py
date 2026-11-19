#!/usr/bin/env python3
"""Actual native eMMC lifecycle plus actual MMC and writer policy, explicit MMIO/media facades."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO, ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument("--cross-compile",action="store_true");a=p.parse_args()
e=Evidence("emmc-native-offline");e.wait_idle_seconds=60
source=ROOT/"assets/i9100-backup/mmcblk0-head1mb.img"
if hashlib.sha256(source.read_bytes()).hexdigest()!="879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef":raise RuntimeError("Historical table changed")
fixture=e.out/"i9100_emmc_native_fixture.c";fixture.write_bytes((REPO/"port-tests/i9100_emmc_native_fixture.c").read_bytes())
# Extra include directory resolves nested fixtures; all actual production
# code remains compiled unchanged, with wrapping only at MMIO boundary.
c=["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c","i9100_emmc_host.c"]
cpp=["i9100_emmc_native.cpp","i9100_camera_mmio.cpp"]
# Evidence model API normally runs argv-less; make the fixture path an
# explicit model environment input instead of replacing controller state.
import os
os.environ["I9100_EMMC_HISTORICAL_SOURCE"]=str(source)
e.model_cpp("i9100_emmc_native_test.cpp",c+["i9100_emmc_native_fixture.c"],cpp,
 source_overrides={"i9100_emmc_native_fixture.c":fixture},wrapped_functions=("i9100_camera_mmio_read","i9100_camera_mmio_write"),
 extra_sdk=REPO/"port-tests/emmc-native-sdk",libraries=("pthread",))
e.result["nested_fixtures"]={n:hashlib.sha256((REPO/"port-tests"/n).read_bytes()).hexdigest() for n in
 ("i9100_emmc_native_fixture.c","i9100_emmc_writer_test.c","i9100_emmc_test.c")}
if a.cross_compile:e.cross_compile(c+cpp+["IOS7I9100EMMC.cpp"])
e.finish(c+cpp+["IOS7I9100EMMC.cpp"],"Native5maps/single-host/inherited-pins/currentcard/writer and RO shared reservation; explicit register/HFS/GPT/media/IOKit facades, no actual host/clock/write/mount")
