#!/usr/bin/env python3
"""Actual MFCv5.1 native firmware/header pipeline, explicit hardware and crypto facades."""
import argparse
from i9100_offline_build import Evidence

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
c = ["i9100_mfc.c", "i9100_mfc_layout.c", "i9100_dma_guard.c"]
cpp = ["i9100_mfc_native.cpp", "i9100_camera_mmio.cpp"]
e = Evidence("mfc-native-offline")
e.wait_idle_seconds = 60
e.model_cpp("i9100_mfc_native_test.cpp", c, cpp, libraries=("crypto",),
            wrapped_functions=("i9100_camera_mmio_read", "i9100_camera_mmio_write"))
if a.cross_compile:
    e.cross_compile(c + cpp + ["i9100_bcm_firmware_crypto.c", "i9100_dma_native.cpp"])
e.finish(c + cpp + ["i9100_bcm_firmware_crypto.c", "i9100_dma_native.cpp"],
         "Actual native MFCv5.1 firmware/sysinit/open/header states and private aligned DMA; explicit provider/crypto/register/IOKit facades; no actual firmware execution or decoded picture")
