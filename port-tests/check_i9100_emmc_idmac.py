#!/usr/bin/env python3
"""Execute actual IDMAC32/MMC/guard and retained old writer models with hardware boundaries."""
import argparse
import hashlib
import json
from i9100_offline_build import Evidence,REPO,ROOT,PLATFORM
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('emmc-idmac-offline');e.wait_idle_seconds=60
source=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
if hashlib.sha256(source.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical table changed')
c=['i9100_emmc.c','i9100_emmc_idmac.c','i9100_dma_guard.c','i9100_emmc_layout.c','i9100_emmc_writer.c']
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-I'+str(PLATFORM),str(REPO/'port-tests/i9100_emmc_idmac_test.c'),*[str(PLATFORM/n)for n in c],'-o',str(e.out/'models')],'compile-models')
e.run([str(e.out/'models'),str(source)],'idmac-model-tests')
e.result.update(models='i9100_emmc_idmac_test.c',models_sha256=hashlib.sha256((REPO/'port-tests/i9100_emmc_idmac_test.c').read_bytes()).hexdigest(),
 model_artifact_sha256=hashlib.sha256((e.out/'models').read_bytes()).hexdigest(),nested_fixtures={n:hashlib.sha256((REPO/'port-tests'/n).read_bytes()).hexdigest()for n in ('i9100_emmc_writer_test.c','i9100_emmc_test.c')},
 companion_headers={n:hashlib.sha256((PLATFORM/n).read_bytes()).hexdigest()for n in ('i9100_emmc_sdr.h','i9100_emmc_idmac.h')})
vendor=ROOT/'research/vendor-linux-u1-a0346556/android_kernel_samsung_smdk4210-a0346556698e62405a04b010c4f4197bdc9fec06'
catalogue=json.loads((ROOT/'research/peripheral-reference-20261115/source-tree.json').read_text())
indexed={item['path']:item['sha']for item in catalogue['tree']if item.get('type')=='blob'}
references={}
for name in ('drivers/mmc/host/mshci.h','drivers/mmc/host/mshci.c','drivers/mmc/host/dw_mmc.h','drivers/mmc/host/dw_mmc.c'):
    file=vendor/name;raw=file.read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if indexed.get(name)!=blob:raise RuntimeError('Original IDMAC reference Gitblob changed')
    references[str(file.relative_to(ROOT))]=dict(sha256=hashlib.sha256(raw).hexdigest(),git_blob=blob)
e.result['idmac_reference_sources']=references
if a.cross_compile:e.cross_compile(c)
e.finish(c,'Actual old32bitIDMAC and checked MMC command/grant/cache/buffer guards; explicit NC/register/card/FIFO/time facades, no hardware DMA/cache proof or partition writes')
