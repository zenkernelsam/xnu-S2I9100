#!/usr/bin/env python3
"""Run the actual read-only acquisition stream against retained explicit media models."""
import hashlib
import importlib.util
import json
from i9100_offline_build import Evidence,ROOT,REPO,PLATFORM

e=Evidence('emmc-read-stream-model');e.wait_idle_seconds=60
loader=ROOT/'uniloader-S2I9100';source=loader/'scripts/i9100'
sources=[source/'emmc-read-stream.c',source/'emmc-read-stream.h',REPO/'port-tests/i9100_emmc_read_stream_test.c',
         REPO/'port-tests/i9100_emmc_writer_test.c',REPO/'port-tests/i9100_emmc_test.c']
captured={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
for name in ('i9100_emmc_read_stream_test.c','i9100_emmc_writer_test.c','i9100_emmc_test.c'):
    (e.out/name).write_bytes((REPO/'port-tests'/name).read_bytes())
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined',
       '-fno-sanitize-recover=all','-I'+str(source),'-I'+str(PLATFORM),str(e.out/'i9100_emmc_read_stream_test.c'),
       str(source/'emmc-read-stream.c'),str(PLATFORM/'i9100_emmc.c'),str(PLATFORM/'i9100_emmc_layout.c'),str(PLATFORM/'i9100_emmc_writer.c'),
       '-o',str(e.out/'models')],'compile-read-stream-model')
history=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
if hashlib.sha256(history.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical primary/PIT drift')
e.run([str(e.out/'models'),str(history),str(e.out)],'read-stream-model-tests')
spec=importlib.util.spec_from_file_location('read_evidence',source/'emmc-read-evidence.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
output=loader/'out'/e.out.name
folder,record=m.import_stream(e.out/'inventory.stream',output,'0102030405060708090a0b0c0d0e0f10','Actual C core with historical primary/PIT, synthetic backup/currentCID/metadata boundaries',0)
assert record['result']=='PASS' and record['model_fixture'] and not record['device_observed'] and not record['boot_ready']
assert m.verify_acquisition(folder)['model_fixture']
def reject_stream(raw,name):
    path=e.out/(name+'.stream');path.write_bytes(raw);destination=output.with_name(output.name+'-'+name)
    try:m.import_stream(path,destination,'0102030405060708090a0b0c0d0e0f10','Explicit corrupted acquisition fixture',0)
    except ValueError:assert not(destination/'manifest.json').exists()
    else:raise AssertionError('Corrupted acquisition accepted: '+name)
raw=(e.out/'inventory.stream').read_bytes()
for name,offset in (('header-crc',37),('payload-crc',160),('frame-crc',148)):
    bad=bytearray(raw);bad[offset]^=1;reject_stream(bad,name)
reject_stream(raw+b'trailing','trailing');reject_stream(raw[:-8],'partial-end')
for fault in range(1,5):
    try:m.import_stream(e.out/f'fault-{fault}.stream',output.with_name(output.name+f'-fault{fault}'),'0102030405060708090a0b0c0d0e0f10','Injected explicit failed acquisition',0)
    except ValueError:pass
    else:raise AssertionError('Failed C stream accepted')
complete,small=m.import_stream(e.out/'backup-small.stream',output.with_name(output.name+'-backup-small'),
  '0102030405060708090a0b0c0d0e0f10','Deliberately scaled4MiB disk with exact C chunk/coverage/posttable validation',0)
assert small['model_fixture'] and not small['card']['native_layout_compatible'] and small['whole_user_area']['bytes']==4194304
assert m.verify_acquisition(complete,require_backup=True)['whole_user_area']['bytes']==4194304
try:m.verify_acquisition(complete,require_backup=True,require_device=True)
except ValueError:pass
else:raise AssertionError('Model backup accepted as actual device evidence')
for fault in range(1,4):
    failed=output.with_name(output.name+f'-backup-fault{fault}')
    try:m.import_stream(e.out/f'backup-fault-{fault}.stream',failed,'0102030405060708090a0b0c0d0e0f10','Actual incomplete C backup',0)
    except ValueError:assert not (failed/'manifest.json').exists() and not (failed/'mmcblk0-user.img').exists()
    else:raise AssertionError('Incomplete backup accepted')
for name,expected in captured.items():
    if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=expected:raise RuntimeError('Read stream source drift')
e.result.update(device_contacted=False,read_stream_sources=captured,importer_sha256=hashlib.sha256((source/'emmc-read-evidence.py').read_bytes()).hexdigest(),
  model_artifact_sha256=hashlib.sha256((e.out/'models').read_bytes()).hexdigest(),imported_model=str(folder.relative_to(ROOT)),
  imported_manifest_sha256=hashlib.sha256((folder/'manifest.json').read_bytes()).hexdigest(),
  small_backup_proof=str(complete.relative_to(ROOT)),small_backup_manifest_sha256=hashlib.sha256((complete/'manifest.json').read_bytes()).hexdigest())
e.finish(['i9100_emmc.c','i9100_emmc_layout.c','i9100_emmc_writer.c'],'Actual read-only acquisition protocol/C producer/Python importer; historical primary+explicit synthetic card/backup/metadata, no actual device or backup')
