#!/usr/bin/env python3
"""Sanitizer checks of real charger policy, bounded I2C and ADC/GPIO code."""
from pathlib import Path
from datetime import datetime
import os
import sys
import subprocess
import hashlib,json
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
root = Path(__file__).resolve().parent.parent
platform = root / 'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
work = root/'out'/('power-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));work.mkdir()
flags = ['-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-pie', '-g',
         '-I' + str(platform)]
objects = []
for name in ['i9100_power.c', 'i9100_power_bus.c']:
    obj = work / (name + '.o')
    subprocess.run(['clang', '-std=c99', *flags, '-c', str(platform / name),
                    '-o', str(obj)], check=True)
    objects.append(str(obj))
binary = work / 'power-tests'
subprocess.run(['clang++', '-std=c++11', '-no-pie', *flags, str(root / 'port-tests/i9100_power_test.cpp'),
                *objects, '-o', str(binary)], check=True)
result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
(work/'test.log').write_text(result.stdout+result.stderr,encoding='utf-8')
print(result.stdout.strip(),flush=True)
(work/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,
    source_sha256={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [platform/name for name in ('i9100_power.c','i9100_power.h','i9100_power_bus.c','i9100_charge_adopt.h','i9100_charge_epoch.h','i9100_rtc_time.h')]+[root/'port-tests/i9100_power_test.cpp']},
    limit='Production policy/RTC/epoch and hardware protocol models; no physical charge cycle proven'),indent=2)+'\n',encoding='utf-8')
print('EVIDENCE',work)
