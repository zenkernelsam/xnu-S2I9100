#!/usr/bin/env python3
"""Check the production null-vtable capture; self-task copyin is modeled."""
import os
from pathlib import Path
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
import subprocess
from datetime import datetime
repo=Path(__file__).resolve().parents[1]
text=(repo/'osfmk/arm/trap.c').read_text()
def function(start):
    begin=text.index(start);brace=text.index('{',begin);end=brace+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[begin:end]
source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint64_t user_addr_t;
struct i9100_fault_snapshot {uint32_t type,psr,pc,lr,far;};
volatile uint32_t i9100_quartz_fault_payload[16+1024];
#define OSMemoryBarrier() ((void)0)
static unsigned copies,fail;static unsigned char memory[4096];
static int copyin(user_addr_t address,void *data,unsigned bytes){
    copies++;if(fail || address!=0x374000 || bytes!=4096)return 14;
    memcpy(data,memory,bytes);return 0;
}
'''+function('static int i9100_quartz_fault_matches(')+function('static void i9100_quartz_fault_bytes(')+r'''
int main(void){
    struct i9100_fault_snapshot s={4,0x30,0x34774750,0x34774733,12},bad;
    uint32_t r[13]={0};r[1]=r[5]=0x374fa0;
    for(unsigned n=0;n<10;n++){
        bad=s;uint32_t regs[13];memcpy(regs,r,sizeof(r));
        switch(n){case 0:bad.pc+=2;break;case 1:bad.lr+=2;break;
        case 2:bad.far=8;break;case 3:bad.type=3;break;case 4:bad.psr=0x10;break;
        case 5:regs[0]=4;break;case 6:regs[1]++;break;case 7:regs[1]=regs[5]=0;break;
        case 8:regs[1]=regs[5]=0x40000000;break;case 9:regs[1]=regs[5]=0x374fa1;break;}
        i9100_quartz_fault_bytes(&bad,regs,57,0xc1ddbb00);
        assert(!copies && !i9100_quartz_fault_payload[0]);
    }
    i9100_quartz_fault_bytes(&s,r,0,0xc1ddbb00);assert(!copies);
    memset(memory,0x41,sizeof(memory));uint32_t magic=0xdeafbe03;
    memcpy(memory+16,&magic,4);memset(memory+0xfa0,0,4);
    i9100_quartz_fault_bytes(&s,r,57,0xc1ddbb00);
    assert(copies==1 && i9100_quartz_fault_payload[0]==1);
    assert(i9100_quartz_fault_payload[8]==4096 && !i9100_quartz_fault_payload[9]);
    assert(i9100_quartz_fault_payload[12]==magic && !i9100_quartz_fault_payload[13]);
    assert(!memcmp((const void*)&i9100_quartz_fault_payload[16],memory,4096));
    FILE *f=fopen("quartz.bin","wb");assert(f);
    assert(fwrite((const void*)i9100_quartz_fault_payload,1,sizeof(i9100_quartz_fault_payload),f)==sizeof(i9100_quartz_fault_payload));fclose(f);
    i9100_quartz_fault_bytes(&s,r,116,0xc1ddbc00);assert(copies==1 && i9100_quartz_fault_payload[1]==57);
    memset((void*)i9100_quartz_fault_payload,0,sizeof(i9100_quartz_fault_payload));fail=1;
    i9100_quartz_fault_bytes(&s,r,57,0xc1ddbb00);
    assert(i9100_quartz_fault_payload[0]==1 && !i9100_quartz_fault_payload[8] && i9100_quartz_fault_payload[9]==14);
    puts("PASS production QuartzCore exact-caller/bounds/first-fault capture; copyin modeled");
}
'''
out=repo/'out'/('quartz-fault-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
(out/'check.c').write_text(source)
subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True)
print('EVIDENCE',out)
