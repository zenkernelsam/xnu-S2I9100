#!/usr/bin/env python3
"""Sensor category/window, video plane bounds and MFCv5 lifecycle, offline."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_camera_protocol.c","i9100_mfc.c","i9100_video_layout.c","i9100_sysmmu.c","i9100_dma_guard.c"]
e=Evidence("camera-mfc-offline")
e.model("i9100_camera_mfc_test.c",sources)
if a.cross_compile:e.cross_compile(sources+["i9100_dma_native.cpp"])
e.finish(sources+["i9100_dma_native.cpp"],"Camera protocol,MFC mailbox and private SYSMMU tables/arenas/geometry; no capture/codec test")
