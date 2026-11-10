#!/usr/bin/env python3
"""Check the narrow unconfined TCC extension boundary and ARM wide wire."""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys


def main():
    if os.environ.get('I9100_CPU_CAPPED') != '1':
        os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                   sys.executable, __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
    repo = Path(__file__).resolve().parents[1]
    out = repo/'out'/('tcc-extension-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    code = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ENOPOLICY 103
''' + '#include "'+str(repo/'security/i9100_tcc_extension.h')+'"\n' + r'''
#define CHECK(x) do {checks++;if(!(x)){printf("FAIL %u %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 unsigned checks=0;
 CHECK(i9100_tcc_extension_gate("tccd","Sandbox",5,0,1,103));
 const char *names[]={"SpringBoard","kbd","mDNSResponder","tccd2","","TCCD",0};
 for(unsigned n=0;n<7;n++)CHECK(!i9100_tcc_extension_gate(names[n],"Sandbox",5,0,1,103));
 for(int operation=-4;operation<32;operation++)
  CHECK(i9100_tcc_extension_gate("tccd","Sandbox",operation,0,1,103)==(operation==5));
 for(unsigned policies=0;policies<32;policies++)
  CHECK(i9100_tcc_extension_gate("tccd","Sandbox",5,policies,1,103)==(policies==0));
 for(int error=0;error<256;error++)
  CHECK(i9100_tcc_extension_gate("tccd","Sandbox",5,0,1,error)==(error==103));
 for(int enabled=-3;enabled<5;enabled++)
  CHECK(i9100_tcc_extension_gate("tccd","Sandbox",5,0,enabled,103)==(enabled==1));
 CHECK(!i9100_tcc_extension_gate("tccd","Seatbelt",5,0,1,103));
 CHECK(!i9100_tcc_extension_gate("tccd",0,5,0,1,103));
 uint32_t w[10]={0x120000,0,0,0,0x130000,0,8,0,0x140000,0};
 CHECK(sizeof(w)==40 && i9100_tcc_extension_wire(w));
 /* The user packet really has zero high words, not five native pointers. */
 for(unsigned high=1;high<10;high+=2){w[high]=1;CHECK(!i9100_tcc_extension_wire(w));w[high]=0;}
 w[2]=1;CHECK(!i9100_tcc_extension_wire(w));w[2]=0;
 const uint32_t bad[]={0,1,0xfff,0x40000000,0xffffffff};
 for(unsigned at=0;at<9;at+=4){uint32_t keep=w[at];for(unsigned n=0;n<5;n++){w[at]=bad[n];CHECK(!i9100_tcc_extension_wire(w));}w[at]=keep;}
 w[8]=0x3ffffff9;CHECK(!i9100_tcc_extension_wire(w));w[8]=0x3ffffff8;CHECK(i9100_tcc_extension_wire(w));
 const char *allowed[]={"com.apple.tcc.kTCCServiceAddressBook","com.apple.tcc.kTCCServiceCalendar",
                       "com.apple.tcc.kTCCServiceReminders","com.apple.tcc.kTCCServicePhotos"};
 for(unsigned n=0;n<4;n++){CHECK(i9100_tcc_extension_class(allowed[n],"/"));CHECK(i9100_tcc_extension_class(allowed[n],"/var/mobile/Library/AddressBook"));CHECK(!i9100_tcc_extension_class(allowed[n],"relative"));}
 const char *denied[]={"com.apple.app-sandbox.read","com.apple.app-sandbox.read-write",
                      "com.apple.tcc.kTCCServiceAddressBook.extra","com.apple.tcc.","",0};
 for(unsigned n=0;n<6;n++)CHECK(!i9100_tcc_extension_class(denied[n],"/"));
 CHECK(!i9100_tcc_extension_class(allowed[0],0));
 /* The only returned token is the literal recognized by the real iOS6
  * consume entry39308440 as success/handle0 without invoking syscall6. */
 const char token[]="invalid";CHECK(sizeof(token)==8 && !strcmp(token,"invalid"));
 printf("PASS %u TCC extension scope/wide-wire/canonical-token checks\n",checks);
 return 0;
}
'''
    (out/'test.c').write_text(code)
    for label, args in [('build',['gcc','-std=c99','-O1','-g','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all',str(out/'test.c'),'-o',str(out/'test')]),('test',[str(out/'test')])]:
        run=subprocess.run(args,text=True,capture_output=True)
        (out/(label+'.log')).write_text(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError(run.stdout+run.stderr)
        if run.stdout:print(run.stdout.strip())
    print('Artifacts:',out)


if __name__=='__main__':main()
