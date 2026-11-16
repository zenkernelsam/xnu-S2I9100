#!/usr/bin/env python3
"""CM3 execution/F2/mailbox bootstrap with real upper drivers and card IO stand-in."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_bcm4330.c","i9100_bcm_chip.c","i9100_bcm_core_download.c","i9100_bcm_firmware.c","i9100_bcm_f2.c","i9100_bcm_boot.c"]
helper=REPO/"port-tests/i9100_bcm_wire_model.h"
before=hashlib.sha256(helper.read_bytes()).hexdigest()
e=Evidence("bcm-boot-offline")
e.model("i9100_bcm_boot_test.c",sources,libraries=["crypto"])
if hashlib.sha256(helper.read_bytes()).hexdigest()!=before:raise RuntimeError("Wire fixture changed during test")
e.result["wire_fixture_sha256"]=before
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"Actual upper F1/core/FW/F2/mailbox and data/CDC drivers; card IO and firmware event producer are stand-ins; no device/real FW/RF")
