#!/usr/bin/env python3
"""Pin original Samsung mailbox/DPB sources and execute the host-only tile reference."""
import hashlib,json
from i9100_offline_build import Evidence,REPO,ROOT

e=Evidence('mfc-dpb-reference')
e.wait_idle_seconds=60
pin='a0346556698e62405a04b010c4f4197bdc9fec06'
vendor=ROOT/('research/vendor-linux-u1-a0346556/android_kernel_samsung_smdk4210-'+pin)
tree=json.loads((ROOT/'research/peripheral-reference-20261115/source-tree.json').read_text())
if tree.get('sha')!=pin or tree.get('truncated'):raise RuntimeError('Pinned Samsung source inventory changed')
index={item['path']:item['sha']for item in tree['tree']if item['type']=='blob'}
sources={}
for name in ('regs-mfc-v5.h','s5p_mfc_opr_v5.c','s5p_mfc_opr_v5.h','s5p_mfc.c','s5p_mfc_shm.h','s5p_mfc_reg.h'):
    relative='drivers/media/video/s5p-mfc/'+name
    path=vendor/relative;raw=path.read_bytes()
    blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if blob!=index[relative]:raise RuntimeError('Original Samsung Git blob differs: '+name)
    sources[str(path.relative_to(ROOT))]=dict(sha256=hashlib.sha256(raw).hexdigest(),git_blob=blob,commit=pin)
folder=ROOT/'research/mfc-dpb-reference-20261121'
for name,expected in (('linux-nv12mt.rst','c6f2cfed201bc62064b33663171716e30a2136950a5ac863acaca115bfe03915'),
                      ('gstreamer-tile.c','8d1dfd915705d0fc04abdc89f33d4ccd2b3bd160ba643082af3872b87ec09601')):
    path=folder/name;meta=json.loads(path.with_suffix(path.suffix+'.source.json').read_text())
    if hashlib.sha256(path.read_bytes()).hexdigest()!=expected or meta['sha256']!=expected or not meta['reference_only']:
        raise RuntimeError('Retained original tiling reference changed')
    sources[str(path.relative_to(ROOT))]=dict(sha256=expected,url=meta['url'])
sdk=REPO/'port-tests/mfc-reference-sdk'
test=REPO/'port-tests/i9100_mfc_tile_reference_test.c'
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
       '-I'+str(sdk),'-I'+str(REPO/'port-tests'),str(test),str(folder/'gstreamer-tile.c'),'-o',str(e.out/'tile-reference')],
      'compile-exact-original-reference')
e.run([str(e.out/'tile-reference')],'reference-tests')
e.result.update(mfc_dpb_references=sources,reference_executable_sha256=hashlib.sha256((e.out/'tile-reference').read_bytes()).hexdigest(),
 fixtures={str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest()for p in [test,REPO/'port-tests/i9100_mfc_tile_model.h',*sdk.rglob('*.h')]},
 source_reference_only=True)
e.finish([],'Pinned Samsung v5.1 register/shared/command/DPB blobs and exact original GStreamer host function vs independent cells; reference-only, no XNU linkage or hardware')
