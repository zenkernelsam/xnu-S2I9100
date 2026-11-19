#!/usr/bin/env python3
"""Run real DMA allocation logic with host-only IOKit/PTE/CPU-barrier facades."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO, PLATFORM

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
e = Evidence("dma-native-offline")
e.wait_idle_seconds = 60
raw = (PLATFORM / "i9100_dma_native.cpp").read_bytes()
token = b'__asm__ volatile("dsb sy" ::: "memory");'
if raw.count(token) != 2:
    raise RuntimeError("CPU barrier sites changed; re-audit host-only substitution")
copy = e.out / "i9100_dma_native.cpp"
copy.write_bytes(raw.replace(token, b"OSMemoryBarrier(); /* host-only DSB facade */"))
e.result["host_source_override"] = dict(original_sha256=hashlib.sha256(raw).hexdigest(),
                                      copy_sha256=hashlib.sha256(copy.read_bytes()).hexdigest(),
                                      substitution="two ARM DSB sites only; explicit host OSMemoryBarrier facade")
e.model_cpp("i9100_dma_native_test.cpp", ["i9100_dma_guard.c"], ["i9100_dma_native.cpp"],
            source_overrides={"i9100_dma_native.cpp": copy}, extra_sdk=REPO / "port-tests/dma-native-sdk")
if a.cross_compile:
    e.cross_compile(["i9100_dma_guard.c", "i9100_dma_native.cpp"])
e.finish(["i9100_dma_guard.c", "i9100_dma_native.cpp"],
         "Actual native allocator with IOKit/PTE/DSB host facades; unmodified ARM source compilation; no hardware PTE/cache/contiguity proof")
