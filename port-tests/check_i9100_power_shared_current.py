#!/usr/bin/env python3
"""Compile current paired Power copies and retain the original LPM start block."""
import hashlib
import re
from i9100_offline_build import Evidence, REPO
from i9100_power_shared_copy import prepare
e = Evidence("shared-power-current")
e.wait_idle_seconds = 30
copies = prepare(e)
power_name = "IOS7I9100Power.cpp"
power_path = REPO / "iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform" / power_name
original_bytes = power_path.read_bytes()
original = original_bytes.decode().replace("\r\n", "\n")
matched = re.search(r" int lpm_supported=0;\n.*?\n }\n", original, re.S)
if not matched or copies[power_name].read_text().count(matched[0]) != 1:
    raise RuntimeError("Current original LPM allocation/ready block was altered")
e.cross_compile(["i9100_power_bus.c", power_name, "AppleARMPE.cpp"], copies)
if power_path.read_bytes() != original_bytes:
    raise RuntimeError("Live Power source changed during compilation")
e.result.update(original_lpm_block_sha256=hashlib.sha256(matched[0].encode()).hexdigest(),
                current_live_power_sha256=hashlib.sha256(original_bytes).hexdigest(),
                live_source_modified=False, lpm_block_preserved=True)
e.finish([], "Current paired Power/bus/PE copied integration ARM compiled with original LPM start block preserved; no live application or complete current-main link")
