#!/usr/bin/env python3
"""Prepare opt-in PE internal-root entry; keep live SD/Power/Display unchanged."""
import argparse
import difflib
import hashlib
import json
from i9100_offline_build import REPO,cap_cpu
cap_cpu();p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write-unapplied-patch',action='store_true',required=True);p.parse_args()
name='iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/AppleARMPE.cpp';raw=(REPO/name).read_bytes();text=raw.decode()
def replace(before,after):
    global text
    if text.count(before)!=1:raise RuntimeError('Actual PE boot source anchor changed')
    text=text.replace(before,after,1)
anchor='#define HIGH_SCORE  100000'
replace(anchor,anchor+'\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n#include "i9100_emmc_boot.h"\n#include "i9100_emmc_clock_native.h"\n#endif')
anchor='    ios7leo_prepare_root_file();'
replace(anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (!i9100_emmc_root_selected()) ios7leo_prepare_root_file();
#else
''' + anchor + '\n#endif')
anchor='    ios7i9100_register_sd(this);'
replace(anchor,'''    ios6i9100_register_emmc_clock_observation();
    if (i9100_emmc_root_selected()) ios6i9100_register_emmc_boot(this);
    else ios7i9100_register_sd(this);''')
patch=''.join(difflib.unified_diff(raw.decode().splitlines(True),text.splitlines(True),fromfile='a/'+name,tofile='b/'+name,n=1))
target=REPO/'port-tests/i9100_emmc_boot_integration.patch';target.write_text(patch,encoding='utf8')
target.with_suffix('.json').write_text(json.dumps(dict(sources={name:hashlib.sha256(raw).hexdigest()},
    patch_sha256=hashlib.sha256(patch.encode()).hexdigest(),applied=False,hardware_enabled=False,
    objects=['iokit/i9100_emmc_boot_contract.o','iokit/i9100_emmc_boot.cpo','iokit/i9100_emmc_clock.o','iokit/i9100_emmc_clock_native.cpo','iokit/AppleARMPE.cpo'],
    requirements='All native/pair/BSD/HFS/shutdown objects and independently reviewed DT contract; explicit rd=i9100-system only, no SD root fallback'),indent=2)+'\n',encoding='utf8')
if (REPO/name).read_bytes()!=raw:raise RuntimeError('PE source changed; prepared patch stale')
print('PREPARED unapplied internal-root PE selection; live source unchanged')
