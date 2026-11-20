#!/usr/bin/env python3
"""Compile the actual native emmc alias/root BSD hooks on source copies."""
import argparse
from i9100_offline_build import Evidence
from i9100_emmc_storage_copy import prepare, verify
p=argparse.ArgumentParser(description=__doc__);p.parse_args()
e=Evidence("emmc-service-patch");e.wait_idle_seconds=60
copies=prepare(e)
e.cross_compile(["IOMediaBSDClient.cpp","IOKitBSDInit.cpp","i9100_emmc_publication.cpp","IOS6I9100EMMCBlockDriver.cpp"],copies)
verify(e)
e.finish(["i9100_emmc_alias.cpp","IOS6I9100EMMCStorage.cpp","IOS6I9100EMMCRaw.cpp","IOS6I9100EMMCBlockDriver.cpp"],
    "Exact live BSD media/root sources patched only in out; original devfs handle link/cleanup and root property matching compiled ARM; no platform activation or runtime mount proof")
