#!/usr/bin/env python3
"""Verify retained sensor model/native/ARM/link and paired builder evidence."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM
from i9100_link_snapshot import verify as verify_snapshot


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def current(path):
    path=path.resolve()
    if not path.is_relative_to(REPO/'out'):raise RuntimeError('Proof escaped retained XNU out')
    m=json.loads((path/'manifest.json').read_text())
    if m.get('result')!='PASS':raise RuntimeError('Proof is not complete')
    for name,sha in m.get('sources',{}).items():
        if Path(name).name!=name or digest(PLATFORM/name)!=sha:raise RuntimeError('Compiled/model production input changed: '+name)
    for item in json.loads((path/'compile-commands.json').read_text()) if (path/'compile-commands.json').is_file() else []:
        if '-O1' not in item['arguments'] or digest(ROOT/item['source_file'])!=item['source_sha256']:raise RuntimeError('ARM source proof changed')
        if item['module_header'] and digest(ROOT/item['module_header'])!=item['module_header_sha256']:raise RuntimeError('ARM header proof changed')
    if 'sensor_service_copy' in m:
        p=m['sensor_service_copy']
        if digest(REPO/p['original_hid_source'])!=p['original_hid_sha256']:raise RuntimeError('Original HID source changed')
        for name,sha in p['companion_sha256'].items():
            if digest(REPO/name)!=sha:raise RuntimeError('Paired HID companion changed')
    return path,m


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--model',type=Path,action='append',required=True)
    p.add_argument('--native',type=Path,required=True);p.add_argument('--kernel',type=Path,required=True);a=p.parse_args()
    e=Evidence('sensor-hid-delivery');e.wait_idle_seconds=60
    models={}
    for supplied in a.model:
        path,m=current(supplied);name=str(path.relative_to(ROOT))
        if not m.get('native_model') or digest(REPO/'port-tests'/m['native_model'])!=m['native_model_sha256']:
            raise RuntimeError('Actual model entry is missing/changed')
        for key in ('nested_fixtures','model_helpers_sha256'):
            for fixture,sha in m.get(key,{}).items():
                source=REPO/fixture if fixture.startswith('port-tests/') else REPO/'port-tests'/fixture
                if digest(source)!=sha:raise RuntimeError('Nested model fixture changed')
        for fixture,sha in m.get('facade_headers',{}).items():
            if digest(REPO/'port-tests/offline-native-sdk'/fixture)!=sha:raise RuntimeError('Host SDK fixture changed')
        if m.get('actual_companion_sha256') and digest(PLATFORM/'IOS6I9100SensorHIDService.inc')!=m['actual_companion_sha256']:
            raise RuntimeError('Actual service companion model changed')
        models[name]=digest(path/'manifest.json')
    native_path=a.native.resolve()
    if not native_path.is_relative_to(REPO/'out'):raise RuntimeError('Native evidence escaped out')
    native=json.loads(native_path.read_text());directory=native_path.parent
    if native['result']!='PASS' or digest(REPO/'port-tests/verify_i9100_sensor_hid_native.py')!=native['verifier_sha256']:
        raise RuntimeError('Original native verifier changed')
    if digest(directory/'manifest.json')!=native['model_manifest_sha256']:raise RuntimeError('Native packet producer evidence changed')
    for kind,result in native['results'].items():
        if digest(directory/f'sensor-{kind}.bin')!=result['packet_sha256'] or not result['factory_data_matched']:
            raise RuntimeError('Original decoder/factory/callback bytes are stale')
    kernel_path,kernel=current(a.kernel)
    if not kernel.get('complete_kernel_link') or digest(kernel_path/'mach_kernel.sys')!=kernel['linked_kernel_sha256']:
        raise RuntimeError('Complete kernel SHA changed')
    for snapshot in kernel['frozen_input_manifests']:verify_snapshot(ROOT/snapshot)
    cursor=kernel_path;inherited=kernel;seen=set()
    for _ in range(8):
        if 'sensor_service_copy' in inherited:break
        if cursor in seen or 'base' not in inherited:raise RuntimeError('Paired sensor HID proof is missing')
        seen.add(cursor);cursor=(ROOT/inherited['base']).resolve()
        if not cursor.is_relative_to(REPO/'out'):raise RuntimeError('Paired proof ancestry escaped out')
        inherited=json.loads((cursor/'manifest.json').read_text())
        if inherited.get('result')!='PASS':raise RuntimeError('Paired proof ancestor is incomplete')
    else:raise RuntimeError('Paired proof ancestry is too deep')
    paired=inherited['sensor_service_copy']
    metadata=json.loads((REPO/'port-tests/i9100_sensor_hid_service_integration.json').read_text())
    if metadata['patch_sha256']!=paired['patch_sha256'] or digest(REPO/'port-tests/i9100_sensor_hid_service_integration.patch')!=paired['patch_sha256']:
        raise RuntimeError('Paired service integration patch changed')
    if digest(REPO/paired['original_hid_source'])!=paired['original_hid_sha256']:raise RuntimeError('Paired original HID source changed')
    for name,sha in paired['companion_sha256'].items():
        if digest(REPO/name)!=sha:raise RuntimeError('Paired service/ABI source changed')
    paired_commands=[item for item in json.loads((cursor/'compile-commands.json').read_text()) if item['source']=='IOS7LeoHIDTouch.cpp']
    if len(paired_commands)!=1 or digest(ROOT/paired_commands[0]['source_file'])!=paired_commands[0]['source_sha256']:
        raise RuntimeError('Paired HID object no longer has a unique compiled source proof')
    inventory={}
    for n in ast.parse((REPO/'port-tests/check_i9100_peripheral_all.py').read_text()).body:
        if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('C_SOURCES','CPP_SOURCES'):
            inventory[n.targets[0].id]=ast.literal_eval(n.value)
    names=inventory['C_SOURCES']+inventory['CPP_SOURCES']
    if len(names)!=len(set(names)) or len(names)!=kernel['peripheral_objects']:raise RuntimeError('Optional inventory is inconsistent')
    loader=ROOT/'uniloader-S2I9100';source=loader/'scripts/i9100/prepare-automation.py';patch=loader/'scripts/i9100/peripheral-offline-objects-integration.patch'
    record=json.loads((loader/'scripts/i9100/peripheral-offline-objects-integration.json').read_text())
    objects=['iokit/'+Path(name).stem+('.cpo' if name.endswith('.cpp') else '.o') for name in names]
    if digest(source)!=record['source_sha256'] or digest(patch)!=record['patch_sha256'] or record['new_objects']!=objects:
        raise RuntimeError('Loader current source/patch/inventory changed')
    if record['sensor_hid_service']['paired_rebuild']!='iokit/IOS7LeoHIDTouch.cpo':raise RuntimeError('Paired HID rebuild prerequisite is missing')
    tree=loader/'out'/e.out.name/'builder-copy';target=tree/'scripts/i9100/prepare-automation.py';target.parent.mkdir(parents=True);target.write_bytes(source.read_bytes())
    e.run(['git','apply','--check',str(patch)],'live-loader-apply-check',cwd=loader)
    e.run(['git','apply','--directory='+str(tree.relative_to(loader)),str(patch)],'copy-loader-apply',cwd=loader)
    lists=[ast.literal_eval(n.value) for n in ast.walk(ast.parse(target.read_text())) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='peripheral_objects' for t in n.targets)]
    if lists!=[objects] or digest(source)!=record['source_sha256']:raise RuntimeError('Builder AST/live source changed')
    e.result.update(models=models,native_sha256=digest(native_path),
        paired_hid_proof=dict(path=str(cursor.relative_to(ROOT)),original_sha256=paired['original_hid_sha256'],
                             patch_sha256=paired['patch_sha256'],compiled_object=paired_commands[0]['object']),
        validated_kernel=dict(path=str(kernel_path.relative_to(ROOT)),sha256=kernel['linked_kernel_sha256'],bytes=kernel['kernel_bytes'],objects=kernel['total_link_objects'],peripherals=len(names)),
        loader_source_sha256=record['source_sha256'],loader_patch_sha256=record['patch_sha256'],live_source_modified=False)
    e.finish([], 'Current sensor model/fixture/paired source hashes, original native packet verifier, frozen complete kernel and 63-object loader copy AST validated; no physical deployment')


if __name__=='__main__':main()
