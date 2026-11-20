#!/usr/bin/env python3
"""Verify read-only ARM collector, actual protocol models and canonical backup evidence together."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
from i9100_offline_build import Evidence,ROOT,REPO
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

def record(folder):
    folder=Path(folder).resolve()
    if not folder.is_relative_to(REPO/'out'):raise RuntimeError('Read-only evidence escaped XNU out')
    m=json.loads((folder/'manifest.json').read_text())
    if m.get('result')!='PASS'or m.get('hardware_tested')is not False:raise RuntimeError('Require actual offline evidence')
    return folder,m

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',required=True);p.add_argument('--protocol',required=True)
p.add_argument('--arm',required=True);p.add_argument('--orchestration',required=True);a=p.parse_args()
e=Evidence('emmc-read-delivery');e.wait_idle_seconds=60
build,b=record(a.build);protocol,c=record(a.protocol);arm,r=record(a.arm);orchestration,o=record(a.orchestration)
for name,digest in b['collector_sources'].items():
    if sha(ROOT/name)!=digest:raise RuntimeError('Compiled read-only collector source drift')
for name,item in b['original_linux_reference'].items():
    if sha(ROOT/name)!=item['sha256']:raise RuntimeError('Original ARM Linux EABI reference drift')
binary=ROOT/b['linux_arm_executable']['path']
if sha(binary)!=b['linux_arm_executable']['sha256']or binary.stat().st_size!=b['linux_arm_executable']['bytes']:raise RuntimeError('ARM collector changed')
for name,digest in c['read_stream_sources'].items():
    if sha(ROOT/name)!=digest:raise RuntimeError('Read protocol model/source drift')
if sha(protocol/'models')!=c['model_artifact_sha256']:raise RuntimeError('C protocol sanitizer binary drift')
source=ROOT/'uniloader-S2I9100/scripts/i9100'
if sha(source/'emmc-read-evidence.py')!=c['importer_sha256']:raise RuntimeError('Actual importer drift')
arm_result=arm/'arm-run/result.json';actual=json.loads(arm_result.read_text())
if sha(arm_result)!=r['result_sha256']or actual['test_sha256']!=sha(REPO/'port-tests/i9100_emmc_read_linux_arm.py')or actual['collector_sha256']!=sha(binary):raise RuntimeError('ARM execution proof drift')
if actual['producer_source_sha256']!=b['producer_source_sha256'] or len(actual['cases'])<9 or any(x['write_device_syscalls']or x['remaining_descriptors']for x in actual['cases'].values()):raise RuntimeError('ARM read-only device/cleanup coverage missing')
oro=orchestration/'run/result.json';ort=json.loads(oro.read_text())
if sha(oro)!=o['result_sha256']or ort['fixture_sha256']!=sha(REPO/'port-tests/i9100_emmc_acquisition_test.py'):raise RuntimeError('Read-only plan proof drift')
for name,digest in ort['sources'].items():
    if sha(source/name)!=digest:raise RuntimeError('Current acquisition orchestrator/importer changed')
for name,parent in (('proof_manifest_sha256',protocol),('build_manifest_sha256',build)):
    if ort[name]!=sha(parent/'manifest.json'):raise RuntimeError('Orchestrated acquisition came from another protocol/build')
spec=importlib.util.spec_from_file_location('read_evidence',source/'emmc-read-evidence.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for name,key,require_backup in ((c,'imported_model',False),(c,'small_backup_proof',True),(actual,'imported_model',False)):
    checked=m.verify_acquisition(ROOT/name[key],require_backup)
    if checked['model_fixture']is not True or checked['boot_ready']is not False or checked['device_observed']is not False:raise RuntimeError('Model data promoted to device backup')
for flag in ('backup_plan_requires_current_inventory','complete_model_backup_rehashed','model_backup_refused_for_device','altered_argv_refused','reviewed_boot_missing_or_model_backup_refused'):
    if ort.get(flag)is not True:raise RuntimeError('Acquisition prerequisite coverage missing: '+flag)
e.result.update(device_contacted=False,collector=b['linux_arm_executable'],producer_source_sha256=b['producer_source_sha256'],
  proofs={str(folder.relative_to(ROOT)):sha(folder/'manifest.json')for folder in (build,protocol,arm,orchestration)},
  tooling={name:sha(source/name)for name in ('emmc-acquire.py','emmc-read-evidence.py','emmc-inventory-evidence.py',
    'emmc-boot-contract.py','prepare-emmc-boot.py')},
  runtime_hardware_verified=False,whole_actual_s2_backup_available=False,model_backup_cannot_deploy=True)
e.finish([],'Real compiled Linux ARM collector + actual C acquisition/host importer/ARM execution and one-shot orchestration; no physical device/real full backup')
