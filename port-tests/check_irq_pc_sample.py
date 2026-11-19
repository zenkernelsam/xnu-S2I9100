#!/usr/bin/env python3
"""Check the actual bounded IRQ histogram and all publication guards."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('irq-pc-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "i9100_irq_pc_sample.h"
int main(void){struct i9100_pc_sample s={.version=2,.period=37};
 i9100_pc_sample_record(&s,0x80213455,19);i9100_pc_sample_record(&s,0x80213411,19);
 assert(s.sequence==4 && s.samples==2 && s.used==1 && s.bins[0].count==2 && s.bins[0].last_pc==0x80213411);
 i9100_pc_sample_record(&s,0x80213411,16);assert(s.used==2);unsigned before=s.sequence;
 i9100_pc_sample_record(&s,0x80213411,1);assert(s.sequence==before);
 for(unsigned n=2;n<64;n++)i9100_pc_sample_record(&s,0x80000000U+n*256,19);
 assert(s.used==64);i9100_pc_sample_record(&s,0x90000000,19);assert(s.used==64 && s.overflow==1);
 s.sequence|=1;before=s.samples;i9100_pc_sample_record(&s,0x90000000,19);assert(s.samples==before);
 s.sequence=0xfffffffe;s.bins[0].count=0xffffffff;
 i9100_pc_sample_record(&s,0x80213411,19);assert(s.sequence==0 && s.bins[0].count==0xffffffff);
 s.samples=512;i9100_pc_sample_record(&s,0x99990000,19);
 assert(s.epoch==1 && s.samples==1 && s.used==1 && s.overflow==0 && s.bins[0].count==1);
 puts("PASS production IRQ histogram, native mode grouping, bounded 64 bins, overflow/saturation/odd publication and sequence wrap");}
'''
(out/'check.c').write_text(code)
for n,cmd in enumerate((['clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(repo/'pexpert/arm'),str(out/'check.c'),'-o',str(out/'check')],[str(out/'check')])):
    r=subprocess.run(cmd,text=True,capture_output=True);(out/f'command-{n}.log').write_text(r.stdout+r.stderr)
    if r.returncode:print(r.stderr[-2500:],file=sys.stderr);r.check_returncode()
    if r.stdout:print(r.stdout.strip(),flush=True)
print('EVIDENCE',out,flush=True)
