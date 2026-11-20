#!/usr/bin/env python3
"""Exercise read-only acquisition plans and verified backups using actual C protocol output."""
import argparse
import hashlib
import json
import sys
from i9100_offline_build import Evidence,REPO,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',required=True);p.add_argument('--proof',required=True);a=p.parse_args()
e=Evidence('emmc-acquisition-model');e.wait_idle_seconds=60
e.run([sys.executable,'-B',str(REPO/'port-tests/i9100_emmc_acquisition_test.py'),'--build',a.build,'--proof',a.proof,'--output',str(e.out/'run')],
  'read-only-orchestration-model-tests',cwd=REPO)
result=e.out/'run/result.json';e.result['actual_orchestration']=json.loads(result.read_text());e.result['result_sha256']=hashlib.sha256(result.read_bytes()).hexdigest()
e.finish([],'Actual currentCID/read-only plan/importer/complete model image verification; ADB/device/process boundary modeled; no phone/flash/writes')
