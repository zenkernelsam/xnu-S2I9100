#!/usr/bin/env python3
"""Exercise the production callback association and retention; atomics modeled."""
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                         sys.executable, '-B', __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
repo = Path(__file__).resolve().parents[1]
header = repo / 'pexpert/arm/i9100_ls_callback_diag.h'
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UInt32;
static int busy;
static int OSCompareAndSwap(unsigned old,unsigned next,volatile uint32_t *p){
    if(busy || *p!=old)return 0;*p=next;return 1;
}
#define OSMemoryBarrier() __sync_synchronize()
#define I9100_LS_CALLBACK_IMPLEMENTATION 1
'''
suffix = r'''
static unsigned checks;
static void check(int value,const char *why){++checks;if(!value){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
static void reset(void){memset((void*)i9100_ls_callback_diag,0,sizeof(i9100_ls_callback_diag));
    memset(i9100_ls_callback_pending,0,sizeof(i9100_ls_callback_pending));i9100_ls_callback_controls=0;busy=0;}
static void request(unsigned char *data,const char *bundle){
    memset(data,0,352);uint32_t header[6]={0x1112,288,0x21af,0x1203,0,14012};
    memcpy(data,header,24);data[28]=1;strcpy((char*)data+32,bundle);
}
int main(void){
    unsigned char data[352],reply[48]={0};uint32_t context[6]={0},frames[16]={0};
    request(data,"com.apple.Preferences");reset();
    i9100_ls_callback_received(70,0x1234,0x10000,data,352);
    unsigned token=i9100_ls_callback_lookup(70,0x1234,context);
    check(token && context[0]==token && context[1]==0x10000 && context[5]==1,"Preferences association");
    check(!i9100_ls_callback_lookup(71,0x1234,context) && !i9100_ls_callback_lookup(70,0x1235,context),"PID/thread isolation");
    check(!memcmp((const void*)&i9100_ls_callback_diag[21],data,192),"original request prefix");
    uint32_t fields[20]={0,1,2,70,0x1234,token,0x10000,0x20000,0,0,0,352,0x21af,1};
    i9100_ls_callback_event(fields,data,reply,frames);i9100_ls_callback_lookup(70,0x1234,context);
    check(context[2]==0x20000 && i9100_ls_callback_diag[0]==2,"allocation address retained");
    fields[2]=3;((uint32_t*)reply)[0]=0x12;((uint32_t*)reply)[1]=40;((uint32_t*)reply)[2]=0x21af;
    ((uint32_t*)reply)[5]=14112;((uint32_t*)reply)[8]=0xfffffecf;
    i9100_ls_callback_event(fields,data,reply,frames);
    check(i9100_ls_callback_diag[0]==3,"deallocation event retained");
    check(!memcmp((const void*)&i9100_ls_callback_diag[1+2*96+68],reply,48),"reply bytes including original no-reply code");
    fields[5]++;i9100_ls_callback_event(fields,data,reply,frames);
    check(i9100_ls_callback_diag[0]==3,"stale completion rejected");
    i9100_ls_callback_clear(70,0x1234);check(!i9100_ls_callback_lookup(70,0x1234,context),"unrelated later message clears association");
    reset();request(data,"com.apple.locationd");
    for(unsigned n=0;n<8;n++)i9100_ls_callback_received(70,0x1234,0x10000,data,352);
    check(i9100_ls_callback_diag[0]==2 && !i9100_ls_callback_lookup(70,0x1234,context),"only two control requests retained");
    reset();request(data,"com.apple.Preferences");busy=1;i9100_ls_callback_received(70,0x1234,0x10000,data,352);
    check(!i9100_ls_callback_diag[0],"busy guard cannot overwrite");busy=0;
    i9100_ls_callback_received(70,0x1234,0x3fffff00,data,352);check(!i9100_ls_callback_diag[0],"user range ceiling");
    data[28]=0;i9100_ls_callback_received(70,0x1234,0x10000,data,352);check(!i9100_ls_callback_diag[0],"NDR rejected");
    data[28]=1;memset(data+32,1,256);i9100_ls_callback_received(70,0x1234,0x10000,data,352);
    check(!i9100_ls_callback_diag[0],"unterminated bundle rejected");
    reset();request(data,"com.apple.Preferences");
    for(unsigned n=0;n<70;n++)i9100_ls_callback_received(70,0x1234,0x10000,data,352);
    check(i9100_ls_callback_diag[0]==70,"record ring rolls without losing latest");
    FILE *f=fopen("callback.bin","wb");check(f!=NULL,"fixture opened");
    check(fwrite((const void*)i9100_ls_callback_diag,1,sizeof(i9100_ls_callback_diag),f)==sizeof(i9100_ls_callback_diag),"fixture saved");fclose(f);
    printf("%u production callback association checks passed; atomics modeled\n",checks);
}
'''
out = repo / 'out' / ('ls-callback-check-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
source = out / 'callback.c'
source.write_text(prefix + '\n#include "' + str(header) + '"\n' + suffix)
binary = out / 'callback'
subprocess.run(['clang', '-std=c11', '-O1', '-g', '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', str(source), '-o', str(binary)], check=True)
subprocess.run([str(binary)], cwd=out, check=True)
print('EVIDENCE', out)
