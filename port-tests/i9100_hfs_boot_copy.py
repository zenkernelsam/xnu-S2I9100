"""Source copies and real BSD compiler recipes for optional early HFS mount."""
import hashlib
import json
import os
import re
import shlex
from pathlib import Path
from i9100_offline_build import REPO,ROOT,PLATFORM

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def prepare(e):
    target=REPO/'port-tests/i9100_hfs_boot_integration.patch';meta=json.loads(target.with_suffix('.json').read_text())
    if sha(target)!=meta['patch_sha256'] or any(sha(REPO/name)!=expected for name,expected in
        dict(meta['sources'],**meta['companions']).items()):raise RuntimeError('HFS integration source/companion drift')
    tree=e.out/'hfs-boot-copy';copied={}
    for name in dict(meta['sources'],**meta['companions']):
        p=tree/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((REPO/name).read_bytes())
        if name in meta['sources']:copied[p.name]=p
    prefix=str(tree.relative_to(REPO))
    e.run(['git','apply','--check','--directory='+prefix,str(target)],'hfs-boot-copy-check',cwd=REPO)
    e.run(['git','apply','--directory='+prefix,str(target)],'hfs-boot-copy-apply',cwd=REPO)
    e.result['hfs_boot_copy']=dict(live_sources=meta['sources'],companion_sources=meta['companions'],patch_sha256=meta['patch_sha256'],
        copied_sources={str(p.relative_to(REPO)):sha(p) for p in copied.values()},live_modified=False)
    return copied

def verify(e):
    p=e.result['hfs_boot_copy']
    for name,expected in dict(p['live_sources'],**p['companion_sources'],**p['copied_sources']).items():
        if sha(REPO/name)!=expected:raise RuntimeError('HFS copy/source drift: '+name)
    if sha(REPO/'port-tests/i9100_hfs_boot_integration.patch')!=p['patch_sha256']:raise RuntimeError('HFS patch changed')

def compile_bsd(e,copies):
    """Read-only make -n obtains the ACTUAL per-object BSD recipe; all compiler
    outputs use fresh out paths. No make build/install/delete command executes."""
    build=REPO/'BUILD/obj/RELEASE_ARM_EXYNOS4210_I9100';cwd=build/'bsd/RELEASE'
    tc=ROOT/'assets/i9100-toolchain'
    env=dict(os.environ,TC=str(tc),SDKROOT='/',LD64_WRAPPER=str(tc/'bin/ld64'),SRCROOT=str(REPO),
        OBJROOT=str(REPO/'BUILD/obj'),SYMROOT=str(REPO/'BUILD/sym'),DSTROOT=str(REPO/'BUILD/dst'),
        KERNEL_CONFIG='RELEASE',ARCH_CONFIG='ARM',MACHINE_CONFIG='EXYNOS4210_I9100',TARGET_CONFIGS='RELEASE ARM EXYNOS4210_I9100',TARGET=str(build))
    env['PATH']=str(tc/'bin')+':'+env['PATH']
    makefile=cwd/'Makefile';make_hash=sha(makefile)
    recipe=makefile.read_text();mapping={'vfs_syscalls.c':'bsd/vfs/vfs_syscalls.c','hfs_vfsops.c':'bsd/hfs/hfs_vfsops.c',
        'bsd_init.c':'bsd/kern/bsd_init.c','kern_shutdown.c':'bsd/kern/kern_shutdown.c'}
    for name,source in copies.items():
        if name=='i9100_usbcdc.c':continue # Pexpert compiler is a separate actual template.
        if name not in mapping:raise RuntimeError('Only exact four BSD objects are supported')
        source=Path(source).resolve()
        if not source.is_relative_to(e.out):raise RuntimeError('BSD copy escaped this retained out')
        object_name=Path(name).stem+'.o';original=REPO/mapping[name]
        rule=re.search(r'^'+re.escape(object_name)+r': ([^\n]+)',recipe,re.M)
        if not rule or rule[1].replace('$(SOURCE_DIR)',str(REPO)).strip()!=str(original):raise RuntimeError('Actual BSD source recipe changed')
        output=e.run(['make','-n','-f','Makefile','CFLAGS_RELEASEARM=-O1','RM=/bin/false',
            'SOURCE_DIR='+str(REPO),'RELATIVE_SOURCE_PATH=bsd','-W',str(original),object_name], 'recipe-'+name,env,cwd)
        all_compiles=[shlex.split(line) for line in output.splitlines() if ('clang ' in line or 'clang++ ' in line) and ' -c ' in line]
        chosen=[cmd for cmd in all_compiles if str(original) in cmd]
        extras=[cmd for cmd in all_compiles if str(original) not in cmd]
        if len(chosen)!=1 or extras:raise RuntimeError('Unexpected BSD prerequisites; recipe retained for review')
        command=chosen[0];input_at=command.index(str(original));command[input_at]=str(source)
        if '-O1' not in command or not any('BSD_KERNEL_PRIVATE' in x for x in command):raise RuntimeError('Not the actual optimized BSD kernel recipe')
        destination=e.out/(name+'.o')
        if '-o' in command:command[command.index('-o')+1]=str(destination)
        else:command+=['-o',str(destination)]
        for index,flag in enumerate(command[:-1]):
            if flag in ('-MF','-MT'):command[index+1]=str(destination.with_suffix('.d') if flag=='-MF' else destination)
        command+=['-MF',str(destination.with_suffix('.d')),'-I'+str(original.parent),'-I'+str(REPO/'bsd'),'-I'+str(PLATFORM)]
        before=sha(source);e.run(command,'cross-bsd-'+name,env,cwd)
        if sha(source)!=before:raise RuntimeError('BSD copy changed while compiling')
        e.commands.append(dict(source=name,source_file=str(source.relative_to(ROOT)),source_sha256=before,
            module_header=None,module_header_sha256=None,object=str(destination.relative_to(ROOT)),arguments=command))
    if sha(makefile)!=make_hash:raise RuntimeError('Shared Makefile changed; compiled reference invalid')
    (e.out/'compile-commands.json').write_text(json.dumps(e.commands,indent=2)+'\n')
    e.result['cross_compiled']=True;e.result['bsd_makefile_sha256']=make_hash
    if 'i9100_usbcdc.c' in copies:
        source=Path(copies['i9100_usbcdc.c']).resolve();template=ROOT/'xnu-S2I9100/out/automation/control-20261116-123700/compile-commands.json'
        t=next(item for item in json.loads(template.read_text()) if item['object']=='pexpert/i9100_usbcdc.o')
        command=list(t['arguments']);original=REPO/'pexpert/arm/i9100_usbcdc.c'
        actual=[i for i,x in enumerate(command) if x.endswith('i9100_usbcdc.c')]
        if len(actual)!=1 or '-O1' not in command:raise RuntimeError('USB exact compiler template missing')
        command[actual[0]]=str(source);destination=e.out/'i9100_usbcdc.c.o';command[command.index('-o')+1]=str(destination)
        for index,flag in enumerate(command[:-1]):
            if flag in ('-MF','-MT'):command[index+1]=str(destination.with_suffix('.d') if flag=='-MF' else destination)
        command+=['-MF',str(destination.with_suffix('.d')),'-I'+str(original.parent),'-DI9100_HFS_FROZEN_REFERENCE=1']
        e.result['historical_lpm_disabled_for_offline_link']=True
        before=sha(source);e.run(command,'cross-pexpert-i9100_usbcdc.c',env,cwd)
        if sha(source)!=before:raise RuntimeError('USB copy changed while compiling')
        e.commands.append(dict(source='i9100_usbcdc.c',source_file=str(source.relative_to(ROOT)),source_sha256=before,
            module_header=None,module_header_sha256=None,object=str(destination.relative_to(ROOT)),arguments=command))
        (e.out/'compile-commands.json').write_text(json.dumps(e.commands,indent=2)+'\n')
