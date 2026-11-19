"""Prepare the exact sensor publication patch on a retained source copy."""
import hashlib
import json
from i9100_offline_build import REPO, PLATFORM


def prepare(e):
    m=json.loads((REPO/'port-tests/i9100_sensor_hid_service_integration.json').read_text())
    source=REPO/m['source'];patch=REPO/'port-tests/i9100_sensor_hid_service_integration.patch'
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(source)!=m['source_sha256'] or sha(patch)!=m['patch_sha256']:
        raise RuntimeError('Current HID/patch SHA changed; regenerate before compiling')
    tree=e.out/'sensor-service-copy';copy=tree/m['source'];copy.parent.mkdir(parents=True)
    copy.write_bytes(source.read_bytes())
    companions=[PLATFORM/'IOS6I9100SensorHIDService.inc',*sorted(PLATFORM.glob('i9100_sensor_hid*.h'))]
    captured={str(path.relative_to(REPO)):sha(path) for path in companions}
    for path in companions:(copy.parent/path.name).write_bytes(path.read_bytes())
    e.run(['git','apply','--check','--directory='+str(tree.relative_to(REPO)),str(patch)],'service-copy-apply-check',cwd=REPO)
    e.run(['git','apply','--directory='+str(tree.relative_to(REPO)),str(patch)],'service-copy-apply',cwd=REPO)
    if sha(source)!=m['source_sha256'] or any(sha(REPO/path)!=expected for path,expected in captured.items()):
        raise RuntimeError('Live HID companion inputs changed while preparing copy')
    e.result['sensor_service_copy']=dict(original_hid_source=m['source'],original_hid_sha256=m['source_sha256'],
        patch_sha256=m['patch_sha256'],companion_sha256=captured,patched_only=str(copy.relative_to(REPO)),live_source_modified=False)
    return {'IOS7LeoHIDTouch.cpp':copy}


def verify(e):
    proof=e.result['sensor_service_copy']
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(REPO/proof['original_hid_source'])!=proof['original_hid_sha256'] or any(
            sha(REPO/name)!=value for name,value in proof['companion_sha256'].items()):
        raise RuntimeError('Live HID/companion source changed before final evidence')
