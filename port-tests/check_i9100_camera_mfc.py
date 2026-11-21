#!/usr/bin/env python3
"""Sensor category/window, video plane bounds and MFCv5 lifecycle, offline."""
import argparse
import hashlib
import os
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_camera_protocol.c","i9100_mfc.c","i9100_mfc_decode.c","i9100_mfc_layout.c","i9100_video_layout.c","i9100_sysmmu.c","i9100_dma_guard.c"]
e=Evidence("camera-mfc-offline")
os.environ["I9100_MFC_CORE_OUTPUT"]=str(e.out/"mfc-core.json")
e.wait_idle_seconds=60
e.model("i9100_camera_mfc_test.c",sources,libraries=("crypto",))
if a.cross_compile:e.cross_compile(sources+["i9100_dma_native.cpp"])
e.result["core_output_sha256"]=hashlib.sha256((e.out/"mfc-core.json").read_bytes()).hexdigest()
e.finish(sources+["i9100_dma_native.cpp"],"Camera protocol,MFC mailbox and private SYSMMU tables/arenas/geometry; no capture/codec test")
