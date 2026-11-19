#!/usr/bin/env python3
"""Original iOS6 kernel-control callbacks, actual native worker and UART models."""
import argparse
import hashlib
import json
import os
from pathlib import Path
from i9100_offline_build import Evidence,REPO,PLATFORM
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true')
p.add_argument('--btserver-native',type=Path,help='Feed original ARM-executed termios bytes into the actual control callback')
p.add_argument('--bt-reset-native',type=Path,help='Feed original ARM H4 Reset bytes into actual control/native UART')
p.add_argument('--bt-baud-native',type=Path,help='Feed original ARM default2.4Mbps termios into actual baud handshake')
a=p.parse_args()
e=Evidence('bt-control-offline');e.wait_idle_seconds=60
os.environ['I9100_BT_HANDOFF_OUTPUT']=str(e.out)
os.environ['I9100_BT_BAUD_OUTPUT']=str(e.out)
if a.bt_baud_native:
    path=a.bt_baud_native.resolve();root=REPO.parent
    if not path.is_relative_to(root/'uniloader-S2I9100/out'):raise RuntimeError('Baud proof escaped loader out')
    proof=json.loads((path/'native-baud.json').read_text())
    if proof.get('result')!='PASS' or proof.get('hardware_tested') is not False or proof.get('device_contacted') is not False:
        raise RuntimeError('Require offline actual original baud proof')
    if hashlib.sha256((root/'uniloader-S2I9100/scripts/i9100/verify-ios6-bt-baud.py').read_bytes()).hexdigest()!=proof['verifier_sha256']:
        raise RuntimeError('Baud verifier changed')
    for name,key in [('verify-ios6-btserver.py','uart_verifier'),('research-ios6-btserver.py','macho_analyzer')]:
        if hashlib.sha256((root/'uniloader-S2I9100/scripts/i9100'/name).read_bytes()).hexdigest()!=proof['dependencies'][key]:
            raise RuntimeError('Baud verifier dependency changed')
    os.environ['I9100_BT_NATIVE_BAUD_TERMIOS_HEX']=proof['results']['default-speed']['termios_hex']
    e.result['bt_baud_native']=dict(path=str(path.relative_to(root)),sha256=hashlib.sha256((path/'native-baud.json').read_bytes()).hexdigest())
if a.bt_reset_native:
    path=a.bt_reset_native.resolve();root=REPO.parent
    if not path.is_relative_to(root/'uniloader-S2I9100/out'):raise RuntimeError('Reset evidence escaped loader out')
    proof=json.loads((path/'native-reset.json').read_text())
    if proof.get('result')!='PASS' or proof.get('hardware_tested') is not False or proof.get('device_contacted') is not False:
        raise RuntimeError('Require successful offline original Reset execution')
    for name,key in [('verify-ios6-btserver.py','uart_verifier'),('research-ios6-btserver.py','macho_analyzer')]:
        if hashlib.sha256((root/'uniloader-S2I9100/scripts/i9100'/name).read_bytes()).hexdigest()!=proof['dependencies'][key]:
            raise RuntimeError('Original Reset dependency changed')
    if hashlib.sha256((root/'uniloader-S2I9100/scripts/i9100/verify-ios6-bt-reset.py').read_bytes()).hexdigest()!=proof['verifier_sha256']:
        raise RuntimeError('Reset verifier changed')
    os.environ['I9100_BT_RESET_REQUEST_HEX']=proof['results']['actual-write']['packets'][0]
    e.result['bt_reset_native']=dict(path=str(path.relative_to(root)),sha256=hashlib.sha256((path/'native-reset.json').read_bytes()).hexdigest())
if a.btserver_native:
    path=a.btserver_native.resolve();root=REPO.parent
    if not path.is_relative_to(root/'uniloader-S2I9100/out'):raise RuntimeError('Native BTServer proof escaped loader out')
    proof=json.loads((path/'native-btserver.json').read_text())
    if proof.get('result')!='PASS' or proof.get('hardware_tested') is not False:raise RuntimeError('Require offline original execution')
    for name,key in [('research-ios6-btserver.py','analyzer_sha256'),('verify-ios6-btserver.py','verifier_sha256')]:
        if hashlib.sha256((root/'uniloader-S2I9100/scripts/i9100'/name).read_bytes()).hexdigest()!=proof[key]:raise RuntimeError('Native verifier changed')
    if hashlib.sha256((root/'research/ios6-btserver-20261119/BTServer.bin').read_bytes()).hexdigest()!=proof['original_image_sha256']:
        raise RuntimeError('Original BTServer image changed')
    os.environ['I9100_BT_NATIVE_TERMIOS_HEX']=proof['results']['device-speed-115200']['termios_hex']
    os.environ['I9100_BT_NATIVE_REJECT_HEX']=proof['results']['default-speed']['termios_hex']
    e.result['btserver_native']=dict(path=str(path.relative_to(root)),sha256=hashlib.sha256((path/'native-btserver.json').read_bytes()).hexdigest())
c=['i9100_gpio_broker.c','i9100_clock_gates.c','i9100_irq_registry.c','i9100_peripheral_board.c',
 'i9100_i2c_samsung.c','i9100_uart_bluetooth.c','i9100_sdio.c','i9100_radio_power.c','i9100_irq_cascade.c',
 'i9100_bluetooth_h4.c','i9100_bt_boot.c','i9100_bt_probe.c','i9100_bt_acl.c','i9100_bt_stream.c']
cpp=['i9100_radio_native.cpp','i9100_bt_worker.cpp','i9100_bt_control.cpp']
sdk=REPO/'port-tests/offline-native-sdk';bsd=REPO/'port-tests/bt-control-sdk'
common=['-O1','-Wall','-Wextra','-Werror','-fno-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-I'+str(bsd),'-I'+str(sdk),'-I'+str(PLATFORM)]
objects=[]
for name in c+cpp:
    dst=e.out/('host-'+name+'.o')
    e.run(['clang++' if name.endswith('.cpp') else 'clang','-std=c++11' if name.endswith('.cpp') else '-std=c99',
           *common,'-c',str(PLATFORM/name),'-o',str(dst)],'host-'+name)
    objects.append(str(dst))
e.run(['clang++','-std=c++11',*common,'-no-pie',str(REPO/'port-tests/i9100_bt_control_test.cpp'),*objects,
       '-Wl,--wrap=i9100_uart_bt_init','-Wl,--wrap=i9100_bt_worker_stream_open','-lcrypto','-o',str(e.out/'models')],'compile-native-models')
e.run([str(e.out/'models')],'native-model-tests')
e.result.update(native_model='i9100_bt_control_test.cpp',native_model_sha256=hashlib.sha256((REPO/'port-tests/i9100_bt_control_test.cpp').read_bytes()).hexdigest(),
 nested_fixtures={name:hashlib.sha256((REPO/'port-tests'/name).read_bytes()).hexdigest() for name in
                 ['i9100_bt_worker_test.cpp','i9100_radio_native_test.cpp','i9100_native_parent_model.h']},
 facade_headers={str(path.relative_to(sdk)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(sdk.rglob('*.h'))},
 bsd_facade_headers={str(path.relative_to(bsd)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(bsd.rglob('*.h'))})
e.result['native_handoff_sha256']=hashlib.sha256((e.out/'native-handoff.json').read_bytes()).hexdigest()
e.result['native_baud_sha256']=hashlib.sha256((e.out/'native-baud.json').read_bytes()).hexdigest()
if a.cross_compile:e.cross_compile(['i9100_bt_stream.c','i9100_bt_worker.cpp','i9100_bt_control.cpp','i9100_peripheral_native.cpp','i9100_radio_native.cpp','i9100_uart_bluetooth.c','i9100_bt_boot.c'])
e.finish(c+cpp,'Actual kernel-control/stream/worker/native UART supply and H4; BSD socket/proc/mbuf/libc and electrical register facades, no daemon/RF acceptance')
