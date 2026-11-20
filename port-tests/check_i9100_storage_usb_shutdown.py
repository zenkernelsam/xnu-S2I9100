#!/usr/bin/env python3
"""Execute original USB fixture with optional storage-safe reboot source copy."""
import hashlib
from i9100_offline_build import Evidence,REPO
from i9100_hfs_boot_copy import prepare,verify
e=Evidence('storage-usb-shutdown');e.wait_idle_seconds=60;copies=prepare(e)
usb=copies['i9100_usbcdc.c'];fixture=REPO/'tools/test-i9100-usbcdc.c';raw=fixture.read_bytes();text=raw.decode()
anchor='#include "../pexpert/arm/i9100_usbcdc.c"'
if text.count(anchor)!=1:raise RuntimeError('Original USB fixture include changed')
text=text.replace(anchor,'#include "'+str(usb)+'"',1)
anchor='    fake_panic_calls=0;charging_shutdown_tests();'
if text.count(anchor)!=1:raise RuntimeError('Original USB fixture entry changed')
text=text.replace(anchor,anchor+'\n    storage_shutdown_tests();',1)
anchor='int main('
index=text.index(anchor);text=text[:index]+'#include "i9100_storage_usb_shutdown_test.inc"\n'+text[index:]
source=e.out/'actual-usb-fixture.c';source.write_text(text)
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
 '-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-I'+str(REPO/'pexpert/arm'),'-I'+str(REPO/'tools'),'-I'+str(REPO/'port-tests'),str(source),'-o',str(e.out/'models')],'compile-storage-usb-model')
e.run([str(e.out/'models')],'storage-usb-model-tests');verify(e)
if fixture.read_bytes()!=raw:raise RuntimeError('Original USB fixture changed')
e.result['fixture_sources']={str(path.relative_to(REPO)):hashlib.sha256(path.read_bytes()).hexdigest() for path in
 (fixture,REPO/'port-tests/i9100_storage_usb_shutdown_test.inc')}
e.result['storage_shutdown_protocol_model']=True
e.result['model_artifact_sha256']=hashlib.sha256((e.out/'models').read_bytes()).hexdigest()
e.finish([], 'Actual production USB storage gate and original full USB fixture: ACK/ready ordering, failure blocks/no retry, PMUroute; no hardware or journal proof')
