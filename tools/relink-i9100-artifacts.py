#!/usr/bin/env python3
"""Replace one object while retaining the exact audited core link inputs.

This explicit recovery is for a witnessed compile/link failure, not a cache
for arbitrary source changes. An explicit --baseline-sha256 also permits an
isolated candidate from a known kernel; all other objects deliberately remain
those of that baseline. Old evidence and reused hashes are retained.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
import argparse,hashlib,json,shlex,shutil,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--previous',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--object',required=True)
    p.add_argument('--baseline-sha256',help='Explicit isolated-core candidate from this exact successful Mach-O; no implicit caching');a=p.parse_args()
    previous=a.previous.resolve();out=a.output.resolve();obj=Path(a.object)
    if not previous.is_relative_to(repo/'out') or not out.is_relative_to(repo/'out') or out.exists():p.error('Existing XNU evidence and fresh XNU output required')
    def digest(path):
        value=hashlib.sha256()
        with path.open('rb') as f:
            for chunk in iter(lambda:f.read(65536),b''):value.update(chunk)
        return value.hexdigest()
    if a.baseline_sha256:
        if len(a.baseline_sha256)!=64 or digest(previous/'mach_kernel')!=a.baseline_sha256:p.error('Explicit baseline kernel SHA256 differs')
    elif (previous/'mach_kernel').exists():p.error('A successful baseline requires its explicit --baseline-sha256')
    if len(obj.parts)!=2 or '..' in obj.parts or obj.suffix not in ('.o','.cpo'):p.error('One repository component/object required')
    records=json.loads((previous/'compile-commands.json').read_text());record=next(x for x in records if x['object']==a.object)
    recorded_paths={Path(x['arguments'][x['arguments'].index('-o')+1]).resolve() for x in records}
    if any(not v.is_relative_to(repo/'out') for v in recorded_paths):raise ValueError('Recorded object outside XNU out')
    cmd=list(record['arguments']);old_object=Path(cmd[cmd.index('-o')+1]).resolve()
    if old_object!=previous/obj or '-O1' not in cmd or cmd[0] not in ('/usr/bin/clang','/usr/bin/clang++'):raise ValueError('Unexpected retained compiler command')
    build=repo/'BUILD/obj/RELEASE_ARM_EXYNOS4210_I9100';cwd=build/obj.parts[0]/'RELEASE'
    originals=[Path(x).resolve() for x in (previous/'link.filelist').read_text().splitlines()]
    if originals.count(old_object)!=1:raise ValueError('Repair object is missing or ambiguous in the prior link')
    def fingerprint(path):
        if not (path in recorded_paths or path.is_relative_to(build)) or not path.is_file():raise ValueError('Link input outside the audited build')
        return digest(path)
    hashes={str(v):fingerprint(v) for v in originals+[build/'version.o',build/'lastkernelconstructor.o'] if v!=old_object}
    out.mkdir(parents=True);target=out/obj;target.parent.mkdir()
    cmd[cmd.index('-o')+1]=str(target)
    if '-MF' in cmd:cmd[cmd.index('-MF')+1]=str(target.with_suffix('.d'))
    def run(args,cwd,log):
        result=subprocess.run(args,cwd=cwd,text=True,capture_output=True)
        log.write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError('Repair failed; inspect '+str(log))
    run(cmd,cwd,out/'compile.log')
    filelist=out/'link.filelist';filelist.write_text('\n'.join(str(target if v==old_object else v) for v in originals)+'\n')
    args=shlex.split((previous/'link-flags.txt').read_text().strip().splitlines()[-1])
    if args[0]!='/usr/bin/clang++' or not any(x.startswith('-fuse-ld='+str(repo.parent/'assets/i9100-toolchain/bin/ld64')) for x in args):raise ValueError('Unexpected linker/toolchain')
    args+=['-filelist',str(filelist),str(build/'version.o'),str(build/'lastkernelconstructor.o'),'-o',str(out/'mach_kernel.sys')]
    run(args,build,out/'link.log')
    if any(fingerprint(Path(v))!=expected for v,expected in hashes.items()):raise RuntimeError('Reused link input changed during repair')
    if a.baseline_sha256 and digest(previous/'mach_kernel')!=a.baseline_sha256:raise RuntimeError('Baseline changed during isolated relink')
    shutil.copyfile(out/'mach_kernel.sys',out/'mach_kernel')
    record['arguments']=cmd;(out/'compile-commands.json').write_text(json.dumps(records,indent=2)+'\n')
    report=dict(result='PASS',previous=str(previous),baseline_sha256=a.baseline_sha256,recompiled=a.object,reused_link_inputs=hashes,compiler=cmd,linker=args,
        limit='Other objects intentionally retain the audited baseline; unrelated source changes are not included by this isolated relink')
    (out/'repair.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS repaired '+a.object+' and linked '+str(out/'mach_kernel'),flush=True)
if __name__=='__main__':main()
