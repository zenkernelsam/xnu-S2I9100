#!/usr/bin/env python3
"""Exercise the production LS bootstrap budget against the captured RPC.

This is a narrow compatibility experiment, not an RPC reply implementation.
Retains sanitizer artifacts and never accesses the phone.
"""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys


def extract(text, name):
    pos=text.index(name+'(');begin=text.rfind('static',0,pos)
    brace=text.index('{',pos);depth=1;end=brace+1
    while depth:
        depth += (text[end]=='{') - (text[end]=='}');end+=1
    return text[begin:end]+'\n'


def main():
    if os.environ.get('I9100_CPU_CAPPED')!='1':
        os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
            sys.executable,__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
    repo=Path(__file__).resolve().parents[1]
    out=repo/'out'/('ls-bootstrap-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    source=(repo/'osfmk/ipc/mach_msg.c').read_text()
    function=extract(source,'i9100_ls_bootstrap_budget')
    harness=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t mach_msg_option_t,mach_msg_size_t,mach_msg_timeout_t;
typedef struct {uint32_t msgh_bits,msgh_size,remote,local,reserved;int32_t msgh_id;} mach_msg_header_t;
#define MACH_SEND_MSG 1
#define MACH_RCV_MSG 2
#define MACH_MSG_TYPE_COPY_SEND 19
#define MACH_MSG_TYPE_MAKE_SEND_ONCE 21
#define MACH_MSGH_BITS(r,l) ((r)|((l)<<8))
static unsigned checks;
#define CHECK(c) do {checks++;if(!(c)){printf("FAIL %u %s\n",__LINE__,#c);return 1;}}while(0)
'''+function+r'''
int main(void){
 mach_msg_header_t h={0x1513,0,0x1903,0x1607,0,14007};
 CHECK(sizeof(h)==24);
 /* Actual installd msgbuf2011437c: options3/send24/recv0x540/timeout0. */
 CHECK(i9100_ls_bootstrap_budget(&h,1,0,"installd",3,24,0x540,0)==250);
 CHECK(!i9100_ls_bootstrap_budget(0,1,0,"installd",3,24,0x540,0));
 CHECK(!i9100_ls_bootstrap_budget(&h,1,0,0,3,24,0x540,0));
 const char *callers[]={"SpringBoard","lsd","launchd","installd2","","install"};
 for(unsigned i=0;i<sizeof(callers)/sizeof(*callers);i++)
  CHECK(!i9100_ls_bootstrap_budget(&h,1,0,callers[i],3,24,0x540,0));
 for(int enabled=-1;enabled<=3;enabled++)
  CHECK((i9100_ls_bootstrap_budget(&h,enabled,0,"installd",3,24,0x540,0)!=0)==(enabled==1));
 for(unsigned ready=1;ready<=4;ready++)
  CHECK(!i9100_ls_bootstrap_budget(&h,1,ready,"installd",3,24,0x540,0));
 /* Explicit user options and deadlines are always preserved, including
  * receive-only calls and finite timeouts on unrelated protocols. */
 for(unsigned options=0;options<1024;options++)
  CHECK((i9100_ls_bootstrap_budget(&h,1,0,"installd",options,24,0x540,0)!=0)==(options==3));
 for(unsigned n=0;n<64;n++)
  CHECK((i9100_ls_bootstrap_budget(&h,1,0,"installd",3,n,0x540,0)!=0)==(n==24));
 for(unsigned n=0x500;n<=0x580;n++)
  CHECK((i9100_ls_bootstrap_budget(&h,1,0,"installd",3,24,n,0)!=0)==(n==0x540));
 for(unsigned timeout=1;timeout<256;timeout++)
  CHECK(!i9100_ls_bootstrap_budget(&h,1,0,"installd",3,24,0x540,timeout));
 for(unsigned id=13900;id<14200;id++){
  h.msgh_id=id;
  CHECK((i9100_ls_bootstrap_budget(&h,1,0,"installd",3,24,0x540,0)!=0)==(id==14007));
 }
 h.msgh_id=14007;
 const unsigned bits[]={0,0x13,0x1113,0x1512,0x1514,0x80001513,0x10001513};
 for(unsigned i=0;i<sizeof(bits)/sizeof(*bits);i++){
  h.msgh_bits=bits[i];CHECK(!i9100_ls_bootstrap_budget(&h,1,0,"installd",3,24,0x540,0));
 }
 printf("LS bootstrap budget: %u checks; original MIG handles timeout errors\n",checks);
 return 0;
}
'''
    path=out/'test.c';path.write_text(harness)
    binary=out/'test'
    result=subprocess.run(['gcc','-std=c99','-O1','-g','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all',str(path),'-o',str(binary)],text=True,capture_output=True)
    (out/'build.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    result=subprocess.run([binary],text=True,capture_output=True)
    (out/'test.log').write_text(result.stdout+result.stderr)
    print(result.stdout,flush=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    print('PASS production policy; evidence:',out,flush=True)


if __name__=='__main__':main()
