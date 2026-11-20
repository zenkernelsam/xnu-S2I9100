#!/usr/bin/env python3
"""Execute actual one-shot eMMC SDR transition/restore and independent card model."""
import argparse
import hashlib
import json
from i9100_offline_build import Evidence,REPO,ROOT,PLATFORM
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('emmc-sdr-offline');e.wait_idle_seconds=60
source=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
if hashlib.sha256(source.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical table source changed')
sources=['i9100_emmc.c','i9100_emmc_layout.c','i9100_emmc_writer.c']
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-I'+str(PLATFORM),str(REPO/'port-tests/i9100_emmc_sdr_test.c'),*[str(PLATFORM/n)for n in sources],'-o',str(e.out/'models')],'compile-models')
e.run([str(e.out/'models'),str(source)],'sdr-model-tests')
e.result.update(models='i9100_emmc_sdr_test.c',models_sha256=hashlib.sha256((REPO/'port-tests/i9100_emmc_sdr_test.c').read_bytes()).hexdigest(),
 model_artifact_sha256=hashlib.sha256((e.out/'models').read_bytes()).hexdigest(),
 nested_fixtures={n:hashlib.sha256((REPO/'port-tests'/n).read_bytes()).hexdigest()for n in ('i9100_emmc_test.c','i9100_emmc_writer_test.c')},
 companion_headers={'i9100_emmc_sdr.h':hashlib.sha256((PLATFORM/'i9100_emmc_sdr.h').read_bytes()).hexdigest()})
vendor=ROOT/'research/vendor-linux-u1-a0346556/android_kernel_samsung_smdk4210-a0346556698e62405a04b010c4f4197bdc9fec06'
names=('arch/arm/mach-exynos/mach-u1.c','arch/arm/mach-exynos/setup-mshci-gpio.c','drivers/mmc/core/mmc.c',
 'drivers/mmc/core/mmc_ops.c','drivers/mmc/host/mshci.c','include/linux/mmc/mmc.h')
catalogue=json.loads((ROOT/'research/peripheral-reference-20261115/source-tree.json').read_text())
indexed={entry['path']:entry['sha']for entry in catalogue['tree']if entry.get('type')=='blob'}
refs={}
for name in names:
    path=vendor/name;raw=path.read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if indexed.get(name)!=blob:raise RuntimeError('Original SDR reference Gitblob drift')
    refs[str(path.relative_to(ROOT))]=dict(sha256=hashlib.sha256(raw).hexdigest(),git_blob=blob)
e.result['sdr_reference_sources']=refs
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,'Actual default-off SDR/CMD6/transport/readback/writer/explicit restoration; historical GPT/PIT and synthetic card/time/MMIO; no electrical or persistence proof')
