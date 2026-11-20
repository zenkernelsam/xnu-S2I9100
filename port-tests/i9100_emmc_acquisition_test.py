#!/usr/bin/env python3
"""Execute read-only plan/importer orchestration with actual C streams and process facades."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import zlib
from argparse import Namespace

ROOT=Path(__file__).resolve().parents[2]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'uniloader-S2I9100/scripts/i9100'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
acquire=module('emmc_acquire','emmc-acquire.py');evidence=acquire.evidence

def rejects(call):
    try:call()
    except (ValueError,FileExistsError,acquire.UncertainAcquisition):return
    raise AssertionError('Unsafe acquisition was accepted')

def rewrite_header(raw,nonce,tag):
    raw=bytearray(raw);raw[48:64]=bytes.fromhex(nonce);raw[76:108]=bytes.fromhex(tag)
    struct.pack_into('<I',raw,124,zlib.crc32(raw[:124]));at=128;crc=zlib.crc32(raw[:128])
    while at<len(raw):
        kind,n=struct.unpack_from('<II',raw,at+8);kind&=65535
        if kind==9:
            struct.pack_into('<I',raw,at+32+12,crc)
            struct.pack_into('<I',raw,at+24,zlib.crc32(raw[at+32:at+32+n]));struct.pack_into('<I',raw,at+28,zlib.crc32(raw[at:at+28]));break
        crc=zlib.crc32(raw[at+32:at+32+n],zlib.crc32(raw[at:at+32],crc));at+=32+n
    return bytes(raw)

class Host:
    def __init__(self,source,tag,code=0,truncate=None,uncertain=False):self.source=source;self.tag=tag;self.code=code;self.count=0;self.truncate=truncate;self.uncertain=uncertain
    def run(self,argv,stream,stderr,timeout):
        self.count+=1;assert argv[-2]in ('inventory','backup-user')or argv[-3]=='backup-user'
        nonce=argv[-1]if argv[-2]=='inventory'else argv[-2]
        raw=rewrite_header(self.source,nonce,self.tag);stream.write_bytes(raw if self.truncate is None else raw[:self.truncate]);stderr.write_bytes(b'')
        if self.uncertain:raise acquire.UncertainAcquisition('Explicit process facade remains outstanding')
        return self.code

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--proof',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir();build=a.build.resolve();proof=a.proof.resolve();binary=build/'emmc-read-linux'
    fake_adb=a.output/'modeled-adb';fake_adb.write_bytes(b'Explicit non-executable process fixture; not ADB\n')
    built=json.loads((build/'manifest.json').read_text());tag=built['producer_source_sha256'];source=(proof/'inventory.stream').read_bytes();results={}
    def plan(name,mode='inventory',inventory=None):
        return acquire.create_plan(build,fake_adb,'MODEL-S2',mode,'Actual read-only C stream; Linux/device/ADB explicitly modeled',inventory,
          acquire.OUT/a.output.parent.name/name,True)
    for name,kind in [('success','ok'),('bad-build','tag'),('truncated','truncated'),('process-error','error')]:
        folder,digest=plan(name);host=Host(source,'ff'*32 if kind=='tag' else tag,7 if kind=='error' else 0,120 if kind=='truncated' else None,kind=='uncertain')
        if kind=='ok':
            result=acquire.execute(folder/'plan.json',digest,host);assert result['model_fixture']and not result['device_observed']and not result['write_partitions']
            inventory=folder/'validated';assert evidence.verify_acquisition(inventory)['model_fixture']
        else:
            rejects(lambda:acquire.execute(folder/'plan.json',digest,host));assert not(folder/'result.json').exists()
        rejects(lambda:acquire.execute(folder/'plan.json',digest,host));assert host.count==1
        results[name]=dict(attempts=host.count,no_retry=True,no_device_writes=True)
    folder,digest=plan('not-approved');host=Host(source,tag)
    rejects(lambda:acquire.execute(folder/'plan.json','00'*32,host));assert not host.count and not(folder/'started.json').exists()
    folder,digest=plan('model-cannot-device');rejects(lambda:acquire.execute(folder/'plan.json',digest));assert not(folder/'started.json').exists()
    # Poisoned command cannot be approved by editing JSON and recomputing
    # its SHA: the fixed argv, build/provenance, and mode gate reject first.
    folder,digest=plan('altered-argv');record=json.loads((folder/'plan.json').read_text());record['argv'].append('format')
    (folder/'plan.json').write_text(json.dumps(record));rejects(lambda:acquire.execute(folder/'plan.json',evidence.sha(folder/'plan.json'),Host(source,tag)))
    assert not(folder/'started.json').exists()
    folder,digest=plan('backup-plan',mode='backup-user',inventory=inventory)
    record=json.loads((folder/'plan.json').read_text());assert record['mode']=='backup-user'and record['expected_cid']=='1501000053324939313030009a73abcd'
    record['prior_inventory']=None;(folder/'plan.json').write_text(json.dumps(record))
    host=Host(source,tag);rejects(lambda:acquire.execute(folder/'plan.json',evidence.sha(folder/'plan.json'),host));assert not host.count
    # The small4MiB full image is a scaled coverage proof, never a real S2.
    folder2,small=evidence.import_stream(proof/'backup-small.stream',acquire.OUT/a.output.parent.name/'backup-verification',
      '0102030405060708090a0b0c0d0e0f10','Actual C modeled full coverage with current table checks',0)
    assert evidence.verify_acquisition(folder2,True)['model_fixture'];rejects(lambda:evidence.verify_acquisition(folder2,True,True))
    original=(folder2/'mmcblk0-user.img').read_bytes()
    with(folder2/'mmcblk0-user.img').open('r+b')as f:f.seek(2*1024*1024);f.write(b'bad')
    rejects(lambda:evidence.verify_acquisition(folder2,True));(folder2/'mmcblk0-user.img').write_bytes(original)
    assert evidence.verify_acquisition(folder2,True)['whole_user_area']
    # A claimed-reviewed controller note cannot replace a real restoration
    # copy. Drive the actual preparer down its non-model review branch and
    # prove it rejects before attempting any missing GPT/volume files.
    preparer=module('emmc_prepare','prepare-emmc-boot.py')
    pretend=a.output/'claimed-real-inventory.json'
    claimed=dict(schema=1,soc='exynos4210',machine='GT-I9100',model_fixture=False,clock_and_pins_reviewed=True,
      profile=dict(host={},card={}),evidence={name:{}for name in ('cid','gpt-primary','gpt-backup','pit','controller')})
    pretend.write_text(json.dumps(claimed))
    args=Namespace(inventory=pretend,pair=a.output/'absent-pair.json',source_note='Negative review gate fixture, not device facts',
      mark_reviewed=True,output=a.output/'must-not-exist',backup_acquisition=None)
    try:preparer.prepare(args)
    except ValueError as error:assert 'verified original whole-user backup' in str(error)
    else:raise AssertionError('Reviewed boot preparation bypassed full backup')
    args.backup_acquisition=folder2
    try:preparer.prepare(args)
    except ValueError as error:assert 'original non-model device acquisition' in str(error)
    else:raise AssertionError('Model full backup promoted by claimed controller review')
    assert not args.output.exists()
    folder,digest=plan('uncertain');host=Host(source,tag,uncertain=True)
    rejects(lambda:acquire.execute(folder/'plan.json',digest,host));assert host.count==1 and(folder.parent/'modeled-owner.json').exists()
    rejects(lambda:acquire.execute(folder/'plan.json',digest,host));assert host.count==1
    other,other_sha=plan('uncertain-other-plan');other_host=Host(source,tag)
    rejects(lambda:acquire.execute(other/'plan.json',other_sha,other_host));assert not other_host.count and not(other/'started.json').exists()
    results['uncertain']=dict(attempts=1,second_plan_opener_refused=True,no_kill=True)
    result=dict(result='PASS',hardware_tested=False,device_contacted=False,cases=results,backup_plan_requires_current_inventory=True,
      complete_model_backup_rehashed=True,model_backup_refused_for_device=True,altered_argv_refused=True,
      reviewed_boot_missing_or_model_backup_refused=True,
      sources={n:evidence.sha(ROOT/'uniloader-S2I9100/scripts/i9100'/n)for n in ('emmc-acquire.py','emmc-read-evidence.py','prepare-emmc-boot.py','emmc-boot-contract.py')},
      fixture_sha256=evidence.sha(__file__),proof_manifest_sha256=evidence.sha(proof/'manifest.json'),build_manifest_sha256=evidence.sha(build/'manifest.json'))
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS actual one-shot read-only orchestration/currentCID/model provenance/build-tag/partial/failure/no retry/backup rehash; device and process modeled')
if __name__=='__main__':main()
