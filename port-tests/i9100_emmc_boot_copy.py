"""Compose PE internal-root with original RO and shared-power hooks on out copies."""
import hashlib
import json
from i9100_offline_build import REPO
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def prepare(e):
    tree=e.out/'emmc-boot-pe-copy';patches=[REPO/'port-tests'/name for name in
      ('i9100_emmc_integration.patch','i9100_power_shared_integration.patch','i9100_emmc_boot_integration.patch')]
    metadata=json.loads(patches[-1].with_suffix('.json').read_text())
    if sha(patches[-1])!=metadata['patch_sha256']:raise RuntimeError('PE boot patch changed')
    # Only the PE source from shared-power is paired here. Its actual Power
    # source continues to evolve in the independent mainline; do not import
    # its unrelated ABI into this historical frozen reference.
    shared_text=patches[1].read_text();pe_name='iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/AppleARMPE.cpp'
    start=shared_text.index('--- a/'+pe_name+'\n');tail=shared_text[start:]
    next_patch=tail.find('\n--- a/',1)
    if next_patch>=0:tail=tail[:next_patch+1]
    pe_patch=e.out/'shared-power-pe-only.patch';pe_patch.write_text(tail,encoding='utf8')
    apply_patches=[patches[0],pe_patch,patches[2]]
    paths=set(metadata['sources']);expected=metadata['sources'];captured={};copies={}
    for name in sorted(paths):
        raw=(REPO/name).read_bytes();digest=hashlib.sha256(raw).hexdigest()
        if digest!=expected[name]:raise RuntimeError('PE boot/shared source drift: '+name)
        path=tree/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw);captured[name]=digest;copies[path.name]=path
    name='iokit/conf/files.arm';raw=(REPO/name).read_bytes();path=tree/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw);captured[name]=hashlib.sha256(raw).hexdigest()
    e.run(['git','apply','--check','--directory='+str(tree.relative_to(REPO)),*map(str,apply_patches)],'emmc-boot-pe-check',cwd=REPO)
    e.run(['git','apply','--directory='+str(tree.relative_to(REPO)),*map(str,apply_patches)],'emmc-boot-pe-copy',cwd=REPO)
    e.result['emmc_boot_pe_copy']=dict(live_sources=captured,patches={str(p.relative_to(REPO)):sha(p) for p in apply_patches},
       shared_pe_original=str(patches[1].relative_to(REPO)),shared_pe_fragment_sha256=sha(pe_patch),
       copied_source=str(copies['AppleARMPE.cpp'].relative_to(REPO)),copied_source_sha256=sha(copies['AppleARMPE.cpp']),live_modified=False)
    return {'AppleARMPE.cpp':copies['AppleARMPE.cpp']}
def verify(e):
    p=e.result['emmc_boot_pe_copy']
    if any(sha(REPO/name)!=expected for name,expected in dict(p['live_sources'],**p['patches']).items()) or sha(REPO/p['copied_source'])!=p['copied_source_sha256']:
        raise RuntimeError('Compiled PE boot/shared copy or live source changed')
    original=(REPO/p['shared_pe_original']).read_text();start=original.index('--- a/iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/AppleARMPE.cpp\n')
    selected=original[start:];end=selected.find('\n--- a/',1)
    if end>=0:selected=selected[:end+1]
    if hashlib.sha256(selected.encode()).hexdigest()!=p['shared_pe_fragment_sha256']:raise RuntimeError('Actual shared PE hook drift')
