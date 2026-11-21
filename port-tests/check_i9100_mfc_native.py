#!/usr/bin/env python3
"""Actual MFCv5.1 native firmware/header pipeline, explicit hardware and crypto facades."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO
from i9100_mfc_dma_model import prepare

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
c = ["i9100_mfc.c", "i9100_mfc_decode.c", "i9100_mfc_layout.c", "i9100_dma_guard.c", "i9100_mfc_host.c"]
cpp = ["i9100_mfc_native.cpp", "i9100_camera_mmio.cpp"]
e = Evidence("mfc-native-offline")
e.wait_idle_seconds = 60
e.model_cpp("i9100_mfc_native_test.cpp", c, cpp, libraries=("crypto",),
            wrapped_functions=("i9100_camera_mmio_read", "i9100_camera_mmio_write"),extra_sdk=REPO/'port-tests/dma-native-sdk')
overrides=prepare(e)
e.model_cpp('i9100_mfc_native_test.cpp',c,cpp+['i9100_dma_native.cpp'],libraries=('crypto',),source_overrides=overrides,
 wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),extra_sdk=REPO/'port-tests/dma-native-sdk',extra_defines=('I9100_MFC_ACTUAL_ALLOCATOR=1',))
e.result['actual_allocator_model']=True
e.result['model_preparer_sha256']=hashlib.sha256((REPO/'port-tests/i9100_mfc_dma_model.py').read_bytes()).hexdigest()
if a.cross_compile:
    e.cross_compile(c + cpp + ["i9100_bcm_firmware_crypto.c", "i9100_dma_native.cpp"])
e.finish(c + cpp + ["i9100_bcm_firmware_crypto.c", "i9100_dma_native.cpp"],
         "Actual native MFCv5.1 firmware/header/DPB/frame/lease states and original DMA allocator; provider/crypto/register/IOKit/PTE/DSB host facades; no actual firmware execution or decoded picture")
