#!/usr/bin/env python3
"""Sanitizer checks of real charger policy, bounded I2C and ADC/GPIO code."""
from pathlib import Path
from datetime import datetime
import os
import sys
import subprocess
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
subprocess.run([str(binary)], check=True)
print('EVIDENCE',work)
