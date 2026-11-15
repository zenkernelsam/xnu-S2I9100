#!/usr/bin/env python3
"""Pinned MCLK source and MAUDIO PMU power/lifetime protocol, not live rails."""
import argparse
from i9100_offline_build import Evidence
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
sources = ["i9100_audio_power.c"]
e = Evidence("audio-power-offline")
e.model("i9100_audio_power_test.c", sources)
if a.cross_compile:
    e.cross_compile(sources)
e.finish(sources, "MCLK/MAUDIO protocol and ownership with register model; real temp clocks/rails/MCLK untested")
