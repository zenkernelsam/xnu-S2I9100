#!/usr/bin/env python3
"""Replace selected objects in a verified retained kernel link, without BUILD writes.

Reproduce the supplied baseline byte-for-byte except LC_UUID first. Every reused
object and auxiliary input must retain its SHA throughout this sequential build.
The existing prepare-automation object manifest still governs normal builds.
"""
import argparse, hashlib, json, os, shlex, shutil, struct, subprocess, sys
from pathlib import Path
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1];root=repo.parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path,required=True);p.add_argument('--expect-sha256',required=True)
p.add_argument('--compile-commands',type=Path,help='Additional retained compiler metadata for a baseline object not rebuilt earlier')
p.add_argument('--output',type=Path,required=True);p.add_argument('objects',nargs='+')
a=p.parse_args();base=a.baseline.resolve();out=a.output.resolve()
assert base.is_relative_to(repo/'out') and out.is_relative_to(repo/'out') and not out.exists()
assert len(set(a.objects))==len(a.objects) and all(len(Path(x).parts)==2 and Path(x).suffix in ('.o','.cpo') and '..' not in Path(x).parts for x in a.objects)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def normalized(path):
    raw=bytearray(path.read_bytes());assert struct.unpack_from('<I',raw)[0]==0xfeedface
    at=28;found=0
    for _ in range(struct.unpack_from('<I',raw,16)[0]):
        cmd,n=struct.unpack_from('<2I',raw,at);assert n>=8 and at+n<=len(raw)
        if cmd==27:assert n==24;raw[at+8:at+24]=b'\0'*16;found+=1
        at+=n
    assert found==1;return bytes(raw)
original=base/'mach_kernel.sys';assert sha(original)==a.expect_sha256,'Baseline identity differs'
commands=json.loads((base/'compile-commands.json').read_text());records={x['object']:x for x in commands}
if a.compile_commands:
    extra=a.compile_commands.resolve();assert extra.is_relative_to(repo/'out') and extra.is_file()
    for record in json.loads(extra.read_text()):
        assert record['object'] not in records,'Additional compiler metadata must not replace an existing record'
        records[record['object']]=record
assert all(x in records for x in a.objects),'Missing retained compiler command'
inputs=[Path(x).resolve() for x in (base/'link.filelist').read_text().splitlines()]
assert inputs and len(set(inputs))==len(inputs) and all(x.is_relative_to(repo) and x.is_file() for x in inputs)
build=repo/'BUILD/obj/RELEASE_ARM_EXYNOS4210_I9100'
aux=[build/'version.o',build/'lastkernelconstructor.o']
assert all(x.is_file() for x in aux)
fixed={str(x):sha(x) for x in inputs+aux}
flags=shlex.split((base/'link-flags.txt').read_text().strip().splitlines()[-1])
assert '-O1' not in flags and '-filelist' not in flags and '-o' not in flags
out.mkdir(parents=True);metadata=dict(baseline=str(base),baseline_sha256=a.expect_sha256,
    objects=a.objects,reused_link_inputs=fixed,source_inputs={},checks=[])
def save(): (out/'snapshot.json').write_text(json.dumps(metadata,indent=2)+'\n')
def run(argv,cwd,log):
    result=subprocess.run(argv,cwd=cwd,text=True,capture_output=True)
    (out/log).write_text(result.stdout+result.stderr)
    if result.returncode:
        print(result.stderr[-2500:],file=sys.stderr);save();raise RuntimeError('Command failed: '+log)
def link(paths,destination,log):
    filelist=out/(destination+'.filelist');filelist.write_text('\n'.join(map(str,paths))+'\n')
    run(flags+['-filelist',str(filelist),*map(str,aux),'-o',str(out/destination)],build,log)
link(inputs,'baseline-relinked.sys','baseline-link.log')
assert normalized(out/'baseline-relinked.sys')==normalized(original),'Retained link does not reproduce the baseline'
metadata['checks'].append('Entire baseline reproduced with only LC_UUID normalized')
replacements={};compiled=[]
for item in a.objects:
    record=records[item];args=list(record['arguments']);part,name=Path(item).parts
    cwd=build/part/'RELEASE';assert '-O1' in args and '-c' in args and '-o' in args
    prior=Path(args[args.index('-o')+1]).resolve()
    if prior not in inputs:prior=build/part/'RELEASE'/name
    assert prior in inputs,'Compiler output is not in this baseline link'
    source=Path(next(x for x in args if x.endswith(('.c','.cpp','.s','.S')))).resolve()
    assert source.is_relative_to(repo)
    # The retained dependency list identifies precisely the previously compiled
    # headers. New dependencies are additionally retained after compilation.
    dependencies={source}
    if '-MF' in args:
        olddep=Path(args[args.index('-MF')+1])
        if not olddep.is_absolute():olddep=cwd/olddep
        if olddep.is_file():
            text=olddep.read_text().split(':',1)[1].replace('\\\n',' ')
            dependencies.update((cwd/Path(x)).resolve() for x in shlex.split(text))
    source_before={str(x):sha(x) for x in dependencies if x.is_file()}
    destination=out/item;destination.parent.mkdir(parents=True,exist_ok=True)
    args[args.index('-o')+1]=str(destination)
    if '-MF' in args:args[args.index('-MF')+1]=str(destination.with_suffix('.d'))
    else:args+=['-MF',str(destination.with_suffix('.d'))]
    run(args,cwd,name+'.log')
    assert all(sha(Path(x))==v for x,v in source_before.items()),'Compilation source changed during the build'
    metadata['source_inputs'].update(source_before);compiled.append(dict(object=item,arguments=args))
    replacements[prior]=destination;print('PASS compile',item,flush=True)
assert all(sha(Path(x))==v for x,v in fixed.items()),'Reused link input changed'
paths=[replacements.get(x,x) for x in inputs]
link(paths,'mach_kernel.sys','link.log')
assert all(sha(Path(x))==v for x,v in fixed.items()),'Reused link input changed during link'
shutil.copyfile(out/'mach_kernel.sys',out/'mach_kernel')
(out/'link.filelist').write_text('\n'.join(map(str,paths))+'\n')
shutil.copyfile(base/'link-flags.txt',out/'link-flags.txt')
updated={x['object']:x for x in commands};updated.update({x['object']:x for x in compiled})
(out/'compile-commands.json').write_text(json.dumps(list(updated.values()),indent=2)+'\n')
metadata.update(result='PASS',kernel_sha256=sha(out/'mach_kernel.sys'))
metadata['checks'].append('Selected objects replaced; all other link and auxiliary inputs unchanged')
save();print('PASS snapshot link',metadata['kernel_sha256'],flush=True)
