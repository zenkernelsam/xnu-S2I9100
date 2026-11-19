#!/usr/bin/env python3
"""Bounded original-iOS6 H4 socket stream core; native I/O callbacks are modeled."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cross-compile',action='store_true')
a=p.parse_args()
e=Evidence('bt-stream-offline');e.wait_idle_seconds=60
sources=['i9100_bt_stream.c']
e.model('i9100_bt_stream_test.c',sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,'Actual H4 assembly/44B termios/managed ACL generations/backpressure/lifecycle; callbacks and socket are models, no real BTServer or radio')
