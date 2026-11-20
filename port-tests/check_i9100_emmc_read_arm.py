#!/usr/bin/env python3
"""Run exact compiled ARM collector with syscall facades, not a connected phone."""
import argparse
import hashlib
import json
import sys
from i9100_offline_build import Evidence,REPO,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',required=True);p.add_argument('--table-proof',required=True);a=p.parse_args()
e=Evidence('emmc-read-arm-model');e.wait_idle_seconds=60
e.run([sys.executable,'-B',str(REPO/'port-tests/i9100_emmc_read_linux_arm.py'),'--build',a.build,'--table-proof',a.table_proof,'--output',str(e.out/'arm-run')],
  'actual-arm-read-only-model-tests',cwd=REPO)
r=json.loads((e.out/'arm-run/result.json').read_text());e.result.update(actual_arm_read_model=r,
  result_sha256=hashlib.sha256((e.out/'arm-run/result.json').read_bytes()).hexdigest(),
  collector_build_manifest_sha256=hashlib.sha256((REPO/a.build/'manifest.json').read_bytes()).hexdigest())
e.finish([],'Exact ARM EABI static collector syscalls with explicit Linux/card/blocks facets; no hardware/phone/actual backup')
