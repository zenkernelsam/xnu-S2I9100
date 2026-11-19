#!/usr/bin/env python3
"""Audit current Bluetooth proofs, complete kernel inputs and opt-in builder."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM
from i9100_link_snapshot import verify as verify_snapshot


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def retained(path):
    path=path.resolve()
    if not path.is_relative_to(REPO/'out'):raise RuntimeError('Retained proof escaped XNU out')
    m=json.loads((path/'manifest.json').read_text())
    if m.get('result')!='PASS':raise RuntimeError('Incomplete proof')
    for name,expected in m.get('sources',{}).items():
        if sha(PLATFORM/name)!=expected:raise RuntimeError('Production source/module header changed: '+name)
    for item in json.loads((path/'compile-commands.json').read_text()) if (path/'compile-commands.json').exists() else []:
        if '-O1' not in item['arguments'] or sha(ROOT/item['source_file'])!=item['source_sha256']:raise RuntimeError('ARM source proof changed')
        if item['module_header'] and sha(ROOT/item['module_header'])!=item['module_header_sha256']:raise RuntimeError('ARM header proof changed')
    return path,m


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--model',type=Path,action='append',required=True)
    p.add_argument('--kernel',type=Path,required=True);p.add_argument('--hcd-test',type=Path,required=True)
    p.add_argument('--controller-proof-test',type=Path,help='Verify raw ROM/patched exporter and retained model provenance')
    p.add_argument('--btserver-native',type=Path,help='Audit original BTServer ARM configuration execution evidence')
    p.add_argument('--bt-reset-native',type=Path,help='Audit original H4 Reset and actual native-worker reply decoding')
    a=p.parse_args()
    e=Evidence('bt-delivery');e.wait_idle_seconds=60
    models={}
    for supplied in a.model:
        path,m=retained(supplied)
        for key,hash_key in (('models','models_sha256'),('native_model','native_model_sha256')):
            if m.get(key) and sha(REPO/'port-tests'/m[key])!=m[hash_key]:raise RuntimeError('Model entry changed')
        if not m.get('models') and not m.get('native_model'):raise RuntimeError('Not a model proof')
        for name,digest in m.get('nested_fixtures',{}).items():
            if sha(REPO/'port-tests'/name)!=digest:raise RuntimeError('Nested model fixture changed')
        for name,digest in m.get('facade_headers',{}).items():
            if sha(REPO/'port-tests/offline-native-sdk'/name)!=digest:raise RuntimeError('Host facade changed')
        for name,digest in m.get('bsd_facade_headers',{}).items():
            if sha(REPO/'port-tests/bt-control-sdk'/name)!=digest:raise RuntimeError('BSD socket/proc facade changed')
        for name,digest in m.get('extra_facade_headers',{}).items():
            if sha(REPO/name)!=digest:raise RuntimeError('Additional host facade changed')
        if m.get('rom_probe_json_sha256'):
            report=path/'rom-probe.json'
            if sha(report)!=m['rom_probe_json_sha256']:raise RuntimeError('Raw ROM probe output changed')
            if json.loads(report.read_text()).get('model_fixture') is not True:raise RuntimeError('Modeled ROM output lost its provenance marker')
        if m.get('btserver_native'):
            item=m['btserver_native'];native_path=(ROOT/item['path']).resolve()
            if not native_path.is_relative_to(ROOT/'uniloader-S2I9100/out') or sha(native_path/'native-btserver.json')!=item['sha256']:
                raise RuntimeError('Native ARM-to-control callback evidence changed')
        if m.get('bt_reset_native'):
            item=m['bt_reset_native'];native_path=(ROOT/item['path']).resolve()
            if not native_path.is_relative_to(ROOT/'uniloader-S2I9100/out') or sha(native_path/'native-reset.json')!=item['sha256']:
                raise RuntimeError('Native Reset input evidence changed')
        if m.get('native_handoff_sha256'):
            emitted=path/'native-handoff.json'
            if sha(emitted)!=m['native_handoff_sha256'] or json.loads(emitted.read_text()).get('model_fixture') is not True:
                raise RuntimeError('Actual modeled Reset output changed or lost attribution')
        models[str(path.relative_to(ROOT))]=sha(path/'manifest.json')
    kernel_path,kernel=retained(a.kernel)
    if not kernel.get('complete_kernel_link') or sha(kernel_path/'mach_kernel.sys')!=kernel['linked_kernel_sha256']:
        raise RuntimeError('Complete kernel bytes changed')
    for snapshot in kernel['frozen_input_manifests']:verify_snapshot(ROOT/snapshot)
    names={}
    for n in ast.parse((REPO/'port-tests/check_i9100_peripheral_all.py').read_text()).body:
        if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('C_SOURCES','CPP_SOURCES'):
            names[n.targets[0].id]=ast.literal_eval(n.value)
    inventory=names['C_SOURCES']+names['CPP_SOURCES'];objects=['iokit/'+Path(name).stem+('.cpo' if name.endswith('.cpp') else '.o') for name in inventory]
    if len(inventory)!=len(set(inventory)) or len(inventory)!=kernel['peripheral_objects']:raise RuntimeError('Optional object inventory mismatch')
    loader=ROOT/'uniloader-S2I9100';source=loader/'scripts/i9100/prepare-automation.py';patch=loader/'scripts/i9100/peripheral-offline-objects-integration.patch'
    record=json.loads((loader/'scripts/i9100/peripheral-offline-objects-integration.json').read_text())
    if sha(source)!=record['source_sha256'] or sha(patch)!=record['patch_sha256'] or record['new_objects']!=objects:raise RuntimeError('Current loader source/patch mismatch')
    hcd=a.hcd_test.resolve()
    if not hcd.is_relative_to(loader/'out') or json.loads((hcd/'test-result.json').read_text())['result']!='PASS':raise RuntimeError('HCD preparation model is incomplete')
    proof=json.loads((hcd/'prepared/manifest.json').read_text())
    if proof['device_contacted'] or proof['hardware_tested'] or proof['board_data_reviewed']:raise RuntimeError('Model fixture was marked as real/reviewed')
    if sha(hcd/'fixture.hcd')!=proof['profile']['hcd_sha256'] or sha(hcd/'prepared/firmware.hcd')!=proof['profile']['hcd_sha256']:raise RuntimeError('Retained HCD fixture changed')
    controller_proof=None
    if a.controller_proof_test:
        proof_dir=a.controller_proof_test.resolve()
        if not proof_dir.is_relative_to(loader/'out') or json.loads((proof_dir/'test-result.json').read_text()).get('result')!='PASS':
            raise RuntimeError('Controller proof export test is incomplete')
        export=json.loads((proof_dir/'export/manifest.json').read_text())
        data=json.loads((proof_dir/'export/controller-proof.json').read_text())
        if export['board_data_reviewed'] or not export['model_fixture'] or export['device_contacted'] or not data.get('model_fixture'):
            raise RuntimeError('Model controller identity was marked as real/reviewed')
        if sha(proof_dir/'export/controller-proof.json')!=export['proof_sha256']:raise RuntimeError('Exported controller proof changed')
        controller_proof=str(proof_dir.relative_to(ROOT))
    native_proof=None
    if a.btserver_native:
        native=a.btserver_native.resolve()
        if not native.is_relative_to(loader/'out'):raise RuntimeError('BTServer evidence escaped loader out')
        proof=json.loads((native/'native-btserver.json').read_text())
        image=ROOT/'research/ios6-btserver-20261119/BTServer.bin'
        if proof.get('result')!='PASS' or proof.get('hardware_tested') is not False or proof.get('device_contacted') is not False:
            raise RuntimeError('Native BTServer evidence is incomplete or misattributed')
        if proof['original_image_sha256']!=sha(image) or proof['original_image_uuid']!='af0150d5085e326598edff072a23d146':
            raise RuntimeError('Original BTServer changed')
        for name,key in (('verify-ios6-btserver.py','verifier_sha256'),('research-ios6-btserver.py','analyzer_sha256')):
            if sha(loader/'scripts/i9100'/name)!=proof[key]:raise RuntimeError('Original native verifier changed')
        native_proof=dict(path=str(native.relative_to(ROOT)),sha256=sha(native/'native-btserver.json'))
    reset_proof=None
    if a.bt_reset_native:
        native=a.bt_reset_native.resolve()
        if not native.is_relative_to(loader/'out'):raise RuntimeError('Reset proof escaped loader out')
        proof=json.loads((native/'native-reset.json').read_text())
        if proof.get('result')!='PASS' or proof.get('hardware_tested') is not False or proof.get('device_contacted') is not False:
            raise RuntimeError('Reset execution proof is incomplete')
        if sha(loader/'scripts/i9100/verify-ios6-bt-reset.py')!=proof['verifier_sha256']:
            raise RuntimeError('Actual Reset verifier changed')
        for name,key in [('verify-ios6-btserver.py','uart_verifier'),('research-ios6-btserver.py','macho_analyzer')]:
            if sha(loader/'scripts/i9100'/name)!=proof['dependencies'][key]:raise RuntimeError('Reset decoder dependency changed')
        handoff=proof.get('handoff')
        if not handoff or not handoff['original_reset_completed']:raise RuntimeError('Original decoder never consumed actual native worker output')
        path,manifest=retained(ROOT/handoff['path'])
        if sha(path/'manifest.json')!=handoff['manifest_sha256'] or sha(path/'native-handoff.json')!=handoff['emitted_sha256']:
            raise RuntimeError('Reset handoff bytes/producer changed')
        if manifest['native_model_sha256']!=sha(REPO/'port-tests'/manifest['native_model']):raise RuntimeError('Actual Reset model fixture changed')
        for name,digest in manifest['sources'].items():
            if sha(PLATFORM/name)!=digest:raise RuntimeError('Actual Reset producer changed: '+name)
        reset_proof=dict(path=str(native.relative_to(ROOT)),sha256=sha(native/'native-reset.json'),handoff=handoff)
    tree=loader/'out'/e.out.name/'builder-copy';target=tree/'scripts/i9100/prepare-automation.py';target.parent.mkdir(parents=True);target.write_bytes(source.read_bytes())
    e.run(['git','apply','--check',str(patch)],'loader-live-apply-check',cwd=loader)
    e.run(['git','apply','--directory='+str(tree.relative_to(loader)),str(patch)],'loader-copy-apply',cwd=loader)
    lists=[ast.literal_eval(n.value) for n in ast.walk(ast.parse(target.read_text())) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='peripheral_objects' for t in n.targets)]
    if lists!=[objects] or sha(source)!=record['source_sha256']:raise RuntimeError('Patched builder AST/live source changed')
    e.result.update(model_proofs=models,validated_kernel=dict(path=str(kernel_path.relative_to(ROOT)),bytes=kernel['kernel_bytes'],sha256=kernel['linked_kernel_sha256'],objects=kernel['total_link_objects'],peripherals=len(inventory)),
        loader_source_sha256=record['source_sha256'],loader_patch_sha256=record['patch_sha256'],hcd_fixture=str(hcd.relative_to(ROOT)),
        controller_proof_fixture=controller_proof,btserver_native=native_proof,bt_reset_native=reset_proof,live_builder_modified=False)
    e.finish([], 'Current H4/probe/bootstrap/ACL/native-worker proof, frozen full kernel, HCD/controller export fixtures and live/copied opt-in object inventory; no device/radio/flash')


if __name__=='__main__':main()
