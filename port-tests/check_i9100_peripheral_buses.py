#!/usr/bin/env python3
"""GPIO/I2C electrical models and ARM/XNU object compilation, offline only."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
import argparse,hashlib,json,subprocess
from pathlib import Path
from datetime import datetime
repo=Path(__file__).resolve().parents[1];root=repo.parent;platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
out=repo/'out'/('peripheral-bus-offline-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
sources=['i9100_gpio_broker.c','i9100_i2c_gpio.c','i9100_clock_gates.c','i9100_irq_registry.c','i9100_dma_guard.c']
def idle():
    jobs=[]
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit() or int(entry.name)==os.getpid():continue
        try:name=(entry/'comm').read_text().strip();args=(entry/'cmdline').read_bytes().replace(b'\0',b' ').decode('utf-8','replace')
        except OSError:continue
        if name.startswith(('clang','gcc','cc1','ld64','make','ninja','qemu')) or name.startswith('python') and any(x in args for x in ('prepare-automation.py','rebuild-i9100.py','package-oneshot.py')):jobs.append((entry.name,name))
    if jobs:raise RuntimeError('Another heavy job active; offline build deferred: '+repr(jobs))
def run(command,label,env=None):
    idle();r=subprocess.run(command,cwd=platform,env=env,text=True,capture_output=True)
    (out/(label+'.log')).write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(label+' failed:\n'+(r.stdout+r.stderr)[-4000:])
    print(r.stdout.strip() if label=='tests' else 'PASS '+label,flush=True)
run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
     '-I'+str(platform),str(repo/'port-tests/i9100_peripheral_bus_test.c'),*[str(platform/s) for s in sources],'-o',str(out/'bus-tests')],'compile-tests')
run([str(out/'bus-tests')],'tests')
result=dict(result='PASS',hardware_tested=False,scope='GPIO leases and electrical I2C card model',sources={})
for name in sources+[s.replace('.c','.h') for s in sources]:result['sources'][name]=hashlib.sha256((platform/name).read_bytes()).hexdigest()
if a.cross_compile:
    template=None
    for file in sorted((repo/'out/automation').glob('control-*/compile-commands.json'),reverse=True):
        commands=json.loads(file.read_text());template=next((x for x in commands if x['object']=='iokit/i9100_power.o'),None)
        if template:result['compile_template']=str(file.relative_to(root));result['compile_template_sha256']=hashlib.sha256(file.read_bytes()).hexdigest();break
    if not template:raise RuntimeError('No retained I9100 C compiler template')
    env=dict(os.environ);env['PATH']=str(root/'assets/i9100-toolchain/bin')+':'+env['PATH'];compiled=[]
    for name in sources:
        command=list(template['arguments']);inputs=[i for i,v in enumerate(command) if v.endswith('.c')];assert len(inputs)==1
        command[inputs[0]]=str(platform/name);destination=out/(name+'.o');command[command.index('-o')+1]=str(destination)
        for index,arg in enumerate(command[:-1]):
            if arg in ('-MF','-MT'):command[index+1]=str(destination.with_suffix('.d') if arg=='-MF' else destination)
        command+=['-MF',str(destination.with_suffix('.d'))];assert '-O1' in command
        run(command,'cross-'+name,env);compiled.append(dict(source=name,arguments=command))
    (out/'compile-commands.json').write_text(json.dumps(compiled,indent=2)+'\n');result['cross_compiled']=True
(out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print('EVIDENCE',out,flush=True)
