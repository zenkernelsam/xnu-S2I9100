#!/usr/bin/env python3
"""Validate RTC charge epochs while real shared haptic resources are held.

Only retained out copies get the optional patch. No device/rail activation,
live patch application or second concurrent build is permitted.
"""
import hashlib
from i9100_offline_build import Evidence,REPO,PLATFORM
from i9100_power_shared_copy import prepare
e=Evidence('charge-shared-rtc');e.wait_idle_seconds=60
paths=[PLATFORM/name for name in ('i9100_power.c','i9100_power.h','i9100_power_bus.c',
    'i9100_charge_epoch.h','i9100_charge_adopt.h','i9100_rtc_time.h','i9100_power_shared.cpp','i9100_power_shared.h')]
paths += [REPO/'port-tests'/name for name in ('i9100_power_test.cpp','i9100_power_haptic_model.h','i9100_charge_shared_rtc_test.cpp')]
before={str(path.relative_to(REPO)):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
copies=prepare(e)
e.model_cpp('i9100_charge_shared_rtc_test.cpp',['i9100_power_bus.c','i9100_power.c'],
    ['i9100_power_shared.cpp'],libraries=('pthread',),source_overrides=copies)
for name,digest in before.items():
    if hashlib.sha256((REPO/name).read_bytes()).hexdigest()!=digest:
        raise RuntimeError('Current shared charge source changed during model: '+name)
e.result['current_charge_sources_sha256']=before
e.result['enabled_shared_rtc']=True
e.result['physical_output']=False
e.finish([], 'Production readonly RTC transfer under concurrent retained haptic lease, exact authorization, contention/IRQ refusal and unchanged inherited charge deadline; GPIO/owner/clock modeled')
