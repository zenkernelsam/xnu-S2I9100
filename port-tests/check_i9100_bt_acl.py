#!/usr/bin/env python3
"""Actual HCI ACL credit and connection event state, fault and ARM checks."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('bt-acl-offline');e.wait_idle_seconds=60
e.model('i9100_bt_acl_test.c',['i9100_bt_acl.c'])
if a.cross_compile:e.cross_compile(['i9100_bt_acl.c'])
e.finish(['i9100_bt_acl.c'],'Actual HCI connection/completed/disconnect credit and H4 ACL code; controller events/send are explicit models, no peer/RF/iOS proof')
