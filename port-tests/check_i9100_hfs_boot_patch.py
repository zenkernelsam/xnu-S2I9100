#!/usr/bin/env python3
"""Compile actual VFS/HFS/pre-launchd integration solely on retained copies."""
from i9100_offline_build import Evidence
from i9100_hfs_boot_copy import prepare,verify,compile_bsd
e=Evidence('hfs-boot-offline');e.wait_idle_seconds=60
copies=prepare(e);compile_bsd(e,copies);verify(e)
e.finish([], 'Actual optimized BSD VFS/HFS/earlyinit source-copy objects; no real HFS mount, registry matching, phone, journal persistence or deployment')
