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
for relative in ('arch/arm/mach-exynos/clock-exynos4.c','arch/arm/mach-exynos/clock-exynos4210.c',
                 'arch/arm/mach-exynos/dev-pd.c','arch/arm/mach-exynos/dev-pd-exynos4.c',
                 'arch/arm/mach-exynos/include/mach/regs-clock.h','arch/arm/mach-exynos/include/mach/regs-pmu.h',
                 'arch/arm/plat-samsung/include/plat/cpu.h','drivers/media/video/s5p-mfc/s5p_mfc_pm.c'):
    path=vendor/relative;raw=path.read_bytes()
    blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if blob!=index[relative]:raise RuntimeError('Original MFC power/clock blob differs: '+relative)
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
pll_path=vendor/'arch/arm/plat-s5p/include/plat/pll.h';raw=pll_path.read_bytes()
blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
if blob!=index['arch/arm/plat-s5p/include/plat/pll.h']:raise RuntimeError('Original PLL45 source changed')
sources[str(pll_path.relative_to(ROOT))]=dict(sha256=hashlib.sha256(raw).hexdigest(),git_blob=blob,commit=pin)
text=raw.decode();begin=text.index('#define PLL45XX_MDIV_MASK');end=text.index('#define PLL46XX_KDIV_MASK',begin)
exact=e.out/'original-mfc-pll.c'
exact.write_text('/* Test-only original Samsung GPL2 formula; not linked into XNU. */\n'
 '#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\n#include "i9100_mfc_power.h"\n'
 'typedef uint32_t u32;typedef uint64_t u64;\n#define do_div(n,base) ((n)/=(base))\n'+text[begin:end]+'''
int main(void){
 unsigned checked=0;
 for(unsigned m=25;m<=200;m+=25)for(unsigned p=1;p<=12;p++)for(unsigned s=1;s<=5;s++){
  uint32_t con=0xa0000000U|(m<<16)|(p<<8)|s,rate,div,clock;
  unsigned long original=s5p_get_pll45xx(24000000,con,pll_4508);
  int e=i9100_mfc_power_parent(24000000,256,con,200000000,&rate,&div,&clock);
  if(original<10000000UL||original>1600000000UL||original>3200000000UL)assert(e==I9100_MP_CLOCK);
  else {assert(!e&&rate==original&&div==(original+199999999UL)/200000000UL&&clock==original/div);checked++;}
 }
 printf("PASS exact original Samsung PLL4508 vs actual MFC leaf-rate function: %u valid cases; host reference-only\\n",checked);return 0;}
''')
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
       '-I'+str(REPO/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'),str(exact),
       str(REPO/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_mfc_power.c'),'-o',str(e.out/'original-pll-model')],
      'compile-exact-original-pll')
e.run([str(e.out/'original-pll-model')],'original-pll-tests')
e.result['mfc_original_pll_crossref']=dict(source=str(exact.relative_to(ROOT)),sha256=hashlib.sha256(exact.read_bytes()).hexdigest(),
 model_sha256=hashlib.sha256((e.out/'original-pll-model').read_bytes()).hexdigest())
e.result['sources']['i9100_mfc_power.c']=hashlib.sha256((REPO/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_mfc_power.c').read_bytes()).hexdigest()
e.result['sources']['i9100_mfc_power.h']=hashlib.sha256((REPO/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_mfc_power.h').read_bytes()).hexdigest()
e.finish([],'Pinned Samsung v5.1 register/shared/command/DPB blobs and exact original GStreamer host function vs independent cells; reference-only, no XNU linkage or hardware')
