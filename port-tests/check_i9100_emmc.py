#!/usr/bin/env python3
"""CPU-capped persistent offline MMC/GPT checks; never opens a device."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from datetime import datetime
repo=Path(__file__).resolve().parents[1];root=repo.parent
platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
out=repo/'out'/('emmc-offline-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
def idle():
    jobs=[]
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit() or int(entry.name)==os.getpid():continue
        try:
            name=(entry/'comm').read_text().strip();args=(entry/'cmdline').read_bytes().replace(b'\0',b' ').decode('utf-8','replace')
        except OSError:continue
        if name.startswith(('clang','gcc','cc1','ld64','make','ninja','qemu')) or name.startswith('python') and any(x in args for x in ('prepare-automation.py','rebuild-i9100.py','package-oneshot.py')):jobs.append((entry.name,name))
    if jobs:raise RuntimeError('Another heavy job is active; build deferred: '+repr(jobs))
def run(command,name,cwd=out,env=None):
    idle();r=subprocess.run(command,cwd=cwd,env=env,text=True,capture_output=True)
    (out/(name+'.log')).write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(name+' failed:\n'+(r.stdout+r.stderr)[-4000:])
    print(r.stdout.strip() if name=='tests' else 'PASS '+name,flush=True)
source=root/'assets/i9100-backup/mmcblk0-head1mb.img'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef'
run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
     '-I'+str(platform),str(repo/'port-tests/i9100_emmc_test.c'),str(platform/'i9100_emmc.c'),str(platform/'i9100_emmc_layout.c'),'-o',str(out/'emmc-tests')],'compile-tests')
run([str(out/'emmc-tests'),str(source)],'tests')
result=dict(result='PASS',hardware_tested=False,scope='Offline historical primary GPT/PIT; generated backup GPT and HFS fixtures',sources={})
files=['i9100_emmc.h','i9100_emmc.c','i9100_emmc_layout.h','i9100_emmc_layout.c','IOS7I9100EMMC.cpp','IOS7I9100GPT.cpp']
for name in files:result['sources'][name]=hashlib.sha256((platform/name).read_bytes()).hexdigest()
if a.cross_compile:
    templates=[]
    for file in sorted((repo/'out/automation').glob('control-*/compile-commands.json'),reverse=True):
        data=json.loads(file.read_text());objects={x['object'] for x in data}
        if ('iokit/IOS7I9100SDMMC.cpo' in objects or 'iokit/IOS7I9100Power.cpo' in objects) and 'iokit/i9100_power.o' in objects:
            templates=data;result['compile_template']=str(file.relative_to(root));result['compile_template_sha256']=hashlib.sha256(file.read_bytes()).hexdigest();break
    if not templates:raise RuntimeError('No verified retained i9100 compilation template')
    env=dict(os.environ);env['PATH']=str(root/'assets/i9100-toolchain/bin')+':'+env['PATH']
    commands=[]
    for name in files:
        if name.endswith('.h'):continue
        cpp=name.endswith('.cpp')
        cpp_object='iokit/IOS7I9100SDMMC.cpo' if any(x['object']=='iokit/IOS7I9100SDMMC.cpo' for x in templates) else 'iokit/IOS7I9100Power.cpo'
        template=next(x for x in templates if x['object']==(cpp_object if cpp else 'iokit/i9100_power.o'))
        command=list(template['arguments']);inputs=[i for i,v in enumerate(command) if v.endswith('.cpp' if cpp else '.c')]
        if len(inputs)!=1:raise RuntimeError('Ambiguous template source')
        command[inputs[0]]=str(platform/name);dest=out/(name+('.cpo' if cpp else '.o'))
        command[command.index('-o')+1]=str(dest)
        for index,arg in enumerate(command[:-1]):
            if arg in ('-MF','-MT'):command[index+1]=str(dest.with_suffix('.d') if arg=='-MF' else dest)
        command+=['-MF',str(dest.with_suffix('.d'))]
        assert '-O1' in command
        run(command,'cross-'+name,cwd=platform,env=env);commands.append(dict(source=name,arguments=command))
    (out/'compile-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    result['cross_compiled']=True
(out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
print('EVIDENCE',out,flush=True)
