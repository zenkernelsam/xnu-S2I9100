#!/usr/bin/env python3
"""Rebuild selected existing kernel objects without make's delete/install steps."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    if os.environ.get('I9100_CPU_CAPPED') != '1':
        env = dict(os.environ, I9100_CPU_CAPPED='1')
        os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10',
            'ionice', '-c2', '-n7', sys.executable, __file__, *sys.argv[1:]], env)
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--link', action='store_true')
    parser.add_argument('objects', nargs='+', help='component/object.o')
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(repo / 'out'):
        parser.error('Output must stay inside this repository out')
    output.mkdir(parents=True, exist_ok=True)
    build = repo / 'BUILD/obj/RELEASE_ARM_EXYNOS4210_I9100'
    toolchain = repo.parent / 'assets/i9100-toolchain'
    env = dict(os.environ, TC=str(toolchain), SDKROOT='/',
        LD64_WRAPPER=str(toolchain / 'bin/ld64'), SRCROOT=str(repo),
        OBJROOT=str(repo / 'BUILD/obj'), SYMROOT=str(repo / 'BUILD/sym'),
        DSTROOT=str(repo / 'BUILD/dst'), KERNEL_CONFIG='RELEASE',
        ARCH_CONFIG='ARM', MACHINE_CONFIG='EXYNOS4210_I9100',
        TARGET_CONFIGS='RELEASE ARM EXYNOS4210_I9100')
    env['TARGET'] = str(build)
    env['PATH'] = str(toolchain / 'bin') + ':' + env['PATH']
    commands = []
    for item in args.objects:
        rel = Path(item)
        if len(rel.parts) != 2 or rel.suffix not in ('.o', '.cpo') or '..' in rel.parts:
            parser.error('Objects must be component/object.o')
        cwd = build / rel.parts[0] / 'RELEASE'
        import re
        rule = re.search(r'^' + re.escape(rel.name) + r': ([^\n]+)',
            (cwd / 'Makefile').read_text(), re.M)
        if not rule:
            raise RuntimeError('Missing source rule for ' + item)
        source = rule[1].replace('$(SOURCE_DIR)', str(repo)).strip()
        plan = subprocess.run(['make', '-n', '-f', 'Makefile',
            'CFLAGS_RELEASEARM=-O1', 'RM=/bin/false',
            'SOURCE_DIR=' + str(repo),
            'RELATIVE_SOURCE_PATH=' + rel.parts[0], '-W', source, rel.name],
            cwd=cwd, env=env, text=True, capture_output=True)
        (output / (rel.name + '.plan')).write_text(plan.stdout + plan.stderr)
        if plan.returncode:
            print(plan.stderr[-3000:], file=sys.stderr)
            raise RuntimeError('Could not obtain a compiler command')
        compile_lines = [line for line in plan.stdout.splitlines()
            if ('clang ' in line or 'clang++ ' in line) and ' -c ' in line]
        candidates = [line for line in compile_lines if source in shlex.split(line)]
        if len(compile_lines) > len(candidates):
            # A branch round-trip refreshes source mtimes. make may propose
            # regenerating assym even when its inputs are byte-identical.
            # Recreate it in this output and compare the real definitions;
            # changed structure offsets require a full build, not stale .o.
            extras = [shlex.split(line) for line in compile_lines if line not in candidates]
            if len(extras)!=1 or not any(arg.endswith('/genassym.c') for arg in extras[0]):
                raise RuntimeError('Unexpected prerequisite compile commands; inspect '+rel.name+'.plan')
            generated = output / 'generated-genassym.s'
            command = extras[0]
            command[command.index('-o')+1] = str(generated)
            command += ['-I'+str(repo/'osfmk/libsa'), '-MF', str(output/'generated-genassym.d')]
            result = subprocess.run(command,cwd=cwd,env=env,text=True,capture_output=True)
            (output/'generated-genassym.log').write_text(result.stdout+result.stderr)
            if result.returncode:
                raise RuntimeError('Could not verify generated assembly offsets')
            result = subprocess.run(['sed','-e','/DEFINITION#/!d','-e','s/^.*DEFINITION#//',
                '-e','s/\\$//','-e','p','-e','s/#//2',
                '-e','s/[^A-Za-z0-9_]*\\([A-Za-z0-9_]*\\)/ \\1_NUM/2',str(generated)],
                text=True,capture_output=True,check=True)
            generated_header=output/'generated-assym.s';generated_header.write_text(result.stdout)
            if generated_header.read_bytes() != (cwd/'assym.s').read_bytes():
                raise RuntimeError('Assembly offsets differ from retained BUILD; full rebuild required')
        if len(candidates) != 1:
            raise RuntimeError(f'Expected one compile command; see {rel.name}.plan')
        command = shlex.split(candidates[0])
        if '-O1' not in command and any(arg.endswith(('.s', '.S')) for arg in command):
            command.append('-O1')
        if '-O1' not in command:
            raise RuntimeError('ARM compilation must contain -O1')
        # Clang otherwise writes object/dependency files in cwd. Keep
        # both in the fresh validation directory instead.
        destination = output / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        if '-o' in command:
            command[command.index('-o') + 1] = str(destination)
        else:
            command += ['-o', str(destination)]
        command += ['-MF', str(destination.with_suffix('.d'))]
        # The inherited osfmk template concatenates SOURCE and libsa;
        # standalone invocation has no trailing slash in SOURCE.
        command += ['-I' + str(repo / 'osfmk/libsa')]
        # Security sources include <bsd/bsm/...> from the XNU source root;
        # standalone component invocations do not inherit that search path.
        if rel.parts[0] == 'security':
            command += ['-I' + str(repo)]
        result = subprocess.run(command, cwd=cwd, env=env,
            text=True, capture_output=True)
        (output / (rel.name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stderr[-4000:], file=sys.stderr)
            raise RuntimeError(f'Compilation failed: {item}')
        print(f'PASS compile {item}', flush=True)
        commands.append({'object': item, 'arguments': command})
    (output / 'compile-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if args.link:
        variables = output / 'makevars.mk'
        variables.write_text(f'include {repo}/makedefs/MakeInc.cmd\n'
            f'include {repo}/makedefs/MakeInc.def\n'
            'show_flags:\n\t@echo $(LD) $(LDFLAGS_KERNEL) $(LD_KERNEL_LIBS)\n')
        result = subprocess.run(['make', '-f', str(variables), 'RM=/bin/false',
            'show_flags'], cwd=build, env=env, text=True, capture_output=True, check=True)
        (output / 'link-flags.txt').write_text(result.stdout)
        arguments = shlex.split(result.stdout.strip().splitlines()[-1])
        replacements = {str((build / command['object'].split('/')[0] / 'RELEASE' /
            Path(command['object']).name).resolve()): output / command['object'] for command in commands}
        filelist = output / 'link.filelist'
        originals = [Path(line).resolve() for line in (build / 'link.filelist').read_text().splitlines()]
        filelist.write_text('\n'.join(str(replacements.get(str(path), path)) for path in originals) + '\n')
        arguments += ['-filelist', str(filelist), str(build / 'version.o'),
            str(build / 'lastkernelconstructor.o'), '-o', str(output / 'mach_kernel.sys')]
        result = subprocess.run(arguments, cwd=build, env=env, text=True, capture_output=True)
        (output / 'link.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stderr[-3000:], file=sys.stderr)
            raise RuntimeError('Kernel link failed')
        import shutil
        shutil.copyfile(output / 'mach_kernel.sys', output / 'mach_kernel')
        print('PASS kernel link', flush=True)


if __name__ == '__main__':
    main()
