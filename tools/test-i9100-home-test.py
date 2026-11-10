#!/usr/bin/env python3
"""Verify the optional Home diagnostic emits one bounded pulse only."""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys


def main():
    if os.environ.get('I9100_CPU_CAPPED')!='1':
        os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
            sys.executable,__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
    repo=Path(__file__).resolve().parents[1];out=repo/'out'/('home-test-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir(parents=True)
    header=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/I9100HomeTest.h'
    code='#include <stdint.h>\n#include <stdio.h>\n#include "'+str(header)+'"\n'+r'''
#define CHECK(x) do {checks++;if(!(x)){printf("FAIL %u %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){unsigned checks=0;I9100HomeTest s={0};
 for(unsigned n=0;n<100;n++)CHECK(i9100_home_test_sample(&s,n&1,1,n,50,20)==(n&1));
 s.enabled=1;CHECK(!i9100_home_test_sample(&s,0,0,0,50,20)&&!s.phase);
 CHECK(i9100_home_test_sample(&s,1,1,0,50,20)==1&&!s.phase);
 CHECK(!i9100_home_test_sample(&s,0,1,100,50,20)&&s.phase==1);
 CHECK(!i9100_home_test_sample(&s,0,1,149,50,20));
 CHECK(i9100_home_test_sample(&s,0,1,150,50,20)==1&&s.phase==2);
 CHECK(i9100_home_test_sample(&s,0,1,169,50,20)==1);
 CHECK(!i9100_home_test_sample(&s,0,1,170,50,20)&&s.phase==3);
 for(unsigned n=171;n<400;n++)CHECK(!i9100_home_test_sample(&s,0,1,n,50,20)&&s.phase==3);
 s=(I9100HomeTest){1,0,0};CHECK(!i9100_home_test_sample(&s,0,1,10,50,20));
 CHECK(i9100_home_test_sample(&s,1,1,11,50,20)==1&&s.phase==3);
 s=(I9100HomeTest){1,0,0};CHECK(!i9100_home_test_sample(&s,0,1,10,50,20));
 CHECK(!i9100_home_test_sample(&s,0,0,11,50,20)&&s.phase==3);
 printf("PASS %u Home pulse/default-off/physical-input/reader-close checks\n",checks);return 0;
}
'''
    (out/'test.c').write_text(code)
    for label,args in [('build',['gcc','-std=c99','-O1','-g','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all',str(out/'test.c'),'-o',str(out/'test')]),('test',[str(out/'test')])]:
        run=subprocess.run(args,text=True,capture_output=True);(out/(label+'.log')).write_text(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError(run.stdout+run.stderr)
        if run.stdout:print(run.stdout.strip())
    print('Artifacts:',out)


if __name__=='__main__':main()
