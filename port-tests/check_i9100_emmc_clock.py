#!/usr/bin/env python3
"""Execute the actual read-only4210 PLL/mux/native-clock capture and ARM compiler."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
from i9100_offline_build import Evidence,REPO,PLATFORM,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('emmc-clock-offline');e.wait_idle_seconds=60
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-I'+str(PLATFORM),str(REPO/'port-tests/i9100_emmc_clock_test.c'),str(PLATFORM/'i9100_emmc_clock.c'),'-o',str(e.out/'models')],'compile-clock-model')
e.run([str(e.out/'models'),str(e.out)],'clock-model-tests')
# Use unmodified Samsung rate functions for an independent arithmetic
# cross-reference. Source remains research-only; the host fixture has the
# vendor's own licensing notice and never becomes a production XNU object.
vendor=ROOT/'research/vendor-linux-u1-a0346556/android_kernel_samsung_smdk4210-a0346556698e62405a04b010c4f4197bdc9fec06'
reference_names=('arch/arm/mach-exynos/clock-exynos4.c','arch/arm/mach-exynos/clock-exynos4210.c',
 'arch/arm/mach-exynos/include/mach/regs-clock.h','arch/arm/plat-s5p/include/plat/pll.h','arch/arm/plat-s5p/clock.c',
 'drivers/mmc/host/mshci-s3c.c','arch/arm/mach-exynos/mach-u1.c')
catalogue=json.loads((ROOT/'research/peripheral-reference-20261115/source-tree.json').read_text())
indexed={x['path']:x for x in catalogue['tree']if x.get('type')=='blob'}
references={}
for name in reference_names:
    path=vendor/name;raw=path.read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if not indexed.get(name)or indexed[name]['sha']!=blob:raise RuntimeError('Original Samsung clock source drift')
    references[str(path.relative_to(ROOT))]=dict(sha256=hashlib.sha256(raw).hexdigest(),git_blob=blob)
pll=(vendor/'arch/arm/plat-s5p/include/plat/pll.h').read_text();begin=pll.index('#define PLL45XX_MDIV_MASK');end=pll.index('#define PLL90XX_MDIV_MASK',begin)
fragment=pll[begin:end]
original=e.out/'original-samsung-pll.c'
original.write_text('/* Original Samsung GPL-2.0 source formula; test-only derivative, no product linkage. */\n'
 '#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\ntypedef uint32_t u32;typedef uint64_t u64;\n'
 '#define do_div(n,base) ((n)/=(base))\n'+fragment+'''
int main(){
 assert(s5p_get_pll45xx(24000000,0xa0960301,pll_4508)==1200000000UL);
 assert(s5p_get_pll46xx(24000000,0xa05a0302,32768,pll_4600)==181000000UL);
 assert(s5p_get_pll46xx(24000000,0xa0320301,512,pll_4650c)==202000000UL);
 puts("PASS unmodified original Samsung PLL45/46/4650C rates agree with production capture");return 0;}
''')
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined',str(original),'-o',str(e.out/'original-pll-model')],'compile-original-pll')
e.run([str(e.out/'original-pll-model')],'original-pll-model-tests')
for file in ('dw_mmc-exynos.kernel-org.c','dw_mmc-exynos.kernel-org.h'):
    path=ROOT/'research/emmc-clock-reference-20261120'/file;metadata=json.loads(Path(str(path)+'.source.json').read_text())
    if hashlib.sha256(path.read_bytes()).hexdigest()!=metadata['sha256']:raise RuntimeError('Fixed4210 divisor reference drift')
    references[str(path.relative_to(ROOT))]=dict(sha256=metadata['sha256'],source_url=metadata['url'])
fixed=(ROOT/'research/emmc-clock-reference-20261120/dw_mmc-exynos.kernel-org.h').read_text()
if not re.search(r'#define EXYNOS4210_FIXED_CIU_CLK_DIV\s+2\b',fixed):raise RuntimeError('Upstream fixed4210 divisor changed')
e.result['clock_source_references']=references
e.result['original_samsung_pll_crossref']=dict(source=str(original.relative_to(ROOT)),sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
  model_sha256=hashlib.sha256((e.out/'original-pll-model').read_bytes()).hexdigest())
tool=ROOT/'uniloader-S2I9100/scripts/i9100/emmc-clock-evidence.py'
spec=importlib.util.spec_from_file_location('emmc_clock',tool);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for name,expected in (('clock-mpll.bin',100000000),('clock-epll.bin',90500000),('clock-vpll.bin',101000000)):
    decoded=m.decode((e.out/name).read_bytes(),require_admitted=True);assert decoded['ciu_clock_hz']==expected and decoded['model_fixture']
    try:m.controller((e.out/name).read_bytes(),True,'Model cannot review hardware')
    except ValueError:pass
    else:raise AssertionError('Model clock promoted to hardware')
for n in range(15):
    decoded=m.decode((e.out/f'clock-fault-{n}.bin').read_bytes());assert not decoded['admissible']and decoded['reasons']
disabled=m.decode((e.out/'clock-card-disabled.bin').read_bytes(),require_admitted=True)
assert disabled['ciu_clock_hz']==100000000 and not disabled['card_clock_hz']and not disabled['card_clock_enabled']
selected=m.decode((e.out/'clock-card-source1.bin').read_bytes(),require_admitted=True)
assert selected['host_div']==5 and selected['card_clock_hz']==10000000
e.result['python_decoder_sha256']=hashlib.sha256(tool.read_bytes()).hexdigest()
e.result.update(models='i9100_emmc_clock_test.c',models_sha256=hashlib.sha256((REPO/'port-tests/i9100_emmc_clock_test.c').read_bytes()).hexdigest(),
  model_artifact_sha256=hashlib.sha256((e.out/'models').read_bytes()).hexdigest(),record_outputs={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in sorted(e.out.glob('clock-*.bin'))})
if a.cross_compile:e.cross_compile(['i9100_emmc_clock.c'])
e.finish(['i9100_emmc_clock.c'],'Actual read-only4210 rate/capture with explicit registers/root frequencies/time facades; no physical CIU/GPIO/host/device or boot evidence')
