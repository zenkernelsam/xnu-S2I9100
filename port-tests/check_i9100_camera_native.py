#!/usr/bin/env python3
"""Real native CSIS/FIMC pipeline and DMA ownership with explicit hardware facades."""
import argparse
from i9100_offline_build import Evidence

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
c = ["i9100_camera_engine_io.c", "i9100_csis.c", "i9100_fimc.c",
     "i9100_video_layout.c", "i9100_dma_guard.c"]
cpp = ["i9100_camera_native.cpp", "i9100_camera_mmio.cpp"]
e = Evidence("camera-native-offline")
e.wait_idle_seconds = 60
e.model_cpp("i9100_camera_native_test.cpp", c, cpp,
            wrapped_functions=("i9100_camera_mmio_read", "i9100_camera_mmio_write"))
if a.cross_compile:
    e.cross_compile(c + cpp)
e.finish(c + cpp, "Actual native camera pipeline with explicit IOKit/DMA/supply/register facades; no real sensor init, image, clocks, PMU or hardware")
