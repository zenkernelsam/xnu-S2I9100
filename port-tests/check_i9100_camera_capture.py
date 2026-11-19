#!/usr/bin/env python3
"""Exercise actual Exynos4210 CSIS/FIMCv5.1 single-slot capture, without hardware."""
import argparse
from i9100_offline_build import Evidence

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cross-compile", action="store_true")
args = parser.parse_args()
sources = ["i9100_camera_engine_io.c", "i9100_csis.c", "i9100_fimc.c",
           "i9100_video_layout.c", "i9100_dma_guard.c"]
e = Evidence("camera-capture-offline")
e.wait_idle_seconds = 60
e.model("i9100_camera_capture_test.c", sources)
if args.cross_compile:
    e.cross_compile(sources)
e.finish(sources, "Real CSIS/FIMCv5.1 capture core, explicit register/sensor DMA model; no actual sensor, image, camera service or device")
