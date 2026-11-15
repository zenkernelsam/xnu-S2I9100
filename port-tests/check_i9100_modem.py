#!/usr/bin/env python3
"""Passive XMM6260 USB profile and SIPC4 framing checks without any radio IO."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
e=Evidence("modem-protocol-offline")
e.model("i9100_modem_test.c",["i9100_modem_sipc4.c"])
if a.cross_compile:e.cross_compile(["i9100_modem_sipc4.c"])
e.finish(["i9100_modem_sipc4.c"],"SIPC4/FMT/RAW/RFS and XMM6260 main USB descriptor models; no radio IO or firmware")
