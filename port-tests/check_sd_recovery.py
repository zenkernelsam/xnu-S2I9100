#!/usr/bin/env python3
"""Exercise actual SD recovery with explicit SDHCI/response callbacks, no phone."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import hashlib,json,subprocess
repo=Path(__file__).resolve().parents[1];platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
out=repo/'out'/('sd-recovery-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
(out/'kern').mkdir();(out/'kern/debug.h').write_text('#include <stdarg.h>\nstatic inline int kprintf(const char *format,...){(void)format;return 0;}\n')
commands=(['clang','-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DBOARD_CONFIG_EXYNOS4210_I9100=1','-I'+str(out),'-I'+str(platform),str(platform/'i9100_sdmmc.c'),str(repo/'port-tests/i9100_sd_recovery_test.c'),'-o',str(out/'check')],[str(out/'check')])
for n,args in enumerate(commands):
    r=subprocess.run(args,text=True,capture_output=True);(out/f'command-{n}.log').write_text(r.stdout+r.stderr)
    if r.returncode:print((r.stdout+r.stderr)[-3000:],file=sys.stderr);r.check_returncode()
    if r.stdout:print(r.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,source_sha256={str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (platform/'i9100_sdmmc.c',platform/'i9100_sdmmc.h',repo/'port-tests/i9100_sd_recovery_test.c')},limit='Actual bounded command/recovery and original-CID selection on a modeled SDHCI; no physical SD integrity or full filesystem test'),indent=2)+'\n')
print('EVIDENCE',out)
