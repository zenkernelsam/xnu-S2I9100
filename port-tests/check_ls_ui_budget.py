#!/usr/bin/env python3
"""Exercise production query bounds, peer application and outcome ownership.

IPC send/receive and atomic scheduling are modeled. This does not prove the
phone will advance or that the underlying lsd handler is repaired.
"""
import os
from pathlib import Path
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
import subprocess
from datetime import datetime
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('ls-ui-budget-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t UInt32,mach_msg_option_t,mach_msg_size_t,mach_msg_timeout_t;
typedef struct {uint32_t msgh_bits,msgh_size,remote,local,reserved;int32_t msgh_id;} mach_msg_header_t;
#define MACH_SEND_MSG 1U
#define MACH_RCV_MSG 2U
#define MACH_RCV_TIMEOUT 0x100U
#define MACH_MSG_TYPE_COPY_SEND 19U
#define MACH_MSG_TYPE_MAKE_SEND_ONCE 21U
#define MACH_MSGH_BITS(remote,local) ((remote)|((local)<<8))
static int OSCompareAndSwap(unsigned old,unsigned next,volatile unsigned *p){if(*p!=old)return 0;*p=next;return 1;}
#define OSMemoryBarrier() ((void)0)
#define I9100_LS_UI_IMPLEMENTATION 1
'''+f'#include "{repo}/pexpert/arm/i9100_ls_ui_budget.h"\n'+r'''
struct Query {mach_msg_header_t header;unsigned char ndr[8];char name[256];};
int main(void){
    struct Query q={0};q.header.msgh_bits=0x1513;q.header.msgh_id=14012;
    q.header.msgh_size=0x1707; /* Uninitialized original user size is not authoritative. */
    q.ndr[4]=1;strcpy(q.name,"com.apple.AppStore");struct Query saved=q;
    assert(sizeof(q)==288);
    assert(i9100_ls_ui_pick(&q.header,sizeof(q),1,"SpringBoard",3,288,48,0)==5000);
    for(unsigned n=0;n<13;n++){
        struct Query b=q;unsigned avail=sizeof(b),enabled=1,opt=3,sent=288,recv=48,t=0;
        const char *caller="SpringBoard";
        switch(n){case 0:avail--;break;case 1:enabled=0;break;case 2:caller="lsd";break;
        case 3:b.header.msgh_id=14007;break;case 4:opt=1;break;case 5:opt=0x103;break;
        case 6:sent=284;break;case 7:recv=40;break;case 8:t=50;break;
        case 9:b.header.msgh_bits|=0x80000000;break;case 10:b.ndr[4]=0;break;
        case 11:strcpy(b.name,"com.apple.MobileStore");break;case 12:strcat(b.name,".extra");break;}
        assert(!i9100_ls_ui_pick(&b.header,avail,enabled,caller,opt,sent,recv,t));
    }
    unsigned option=3,timeout=0;
    assert(!i9100_ls_ui_apply(5000,0,&option,&timeout) && option==3 && !timeout);
    assert(i9100_ls_ui_apply(5000,1,&option,&timeout) && option==0x103 && timeout==5000);
    assert(!memcmp(&q,&saved,sizeof(q)));
    i9100_ls_ui_note(56,0x1234,1,0,option);
    i9100_ls_ui_note(56,0x1234,2,0,option);i9100_ls_ui_begin(56,0x1234);
    i9100_ls_ui_begin(56,0x1234);
    i9100_ls_ui_done(57,0x1234,0,option);assert(i9100_ls_ui_diag[0]==2);
    i9100_ls_ui_done(56,0x1235,0,option);assert(i9100_ls_ui_diag[0]==2);
    i9100_ls_ui_done(56,0x1234,0x10004003,option);assert(i9100_ls_ui_diag[0]==3);
    assert(i9100_ls_ui_diag[1+2*8+4]==0x10004003);
    i9100_ls_ui_done(56,0x1234,0,option);assert(i9100_ls_ui_diag[0]==3);
    i9100_ls_ui_note(56,0x5678,1,0,option);
    i9100_ls_ui_note(56,0x5678,2,0x10000003,option); /* Original send failure; no pending receive. */
    i9100_ls_ui_done(56,0x5678,0,option);assert(i9100_ls_ui_diag[0]==5);
    FILE *f=fopen("ui.bin","wb");assert(f);
    assert(fwrite((const void*)i9100_ls_ui_diag,1,sizeof(i9100_ls_ui_diag),f)==sizeof(i9100_ls_ui_diag));fclose(f);
    for(unsigned n=0;n<40;n++)i9100_ls_ui_note(56,0x1234,1,0,option);
    assert(i9100_ls_ui_diag[0]==45);
    puts("PASS production exact query/bounds/peer application and real-result ownership; IPC/atomics modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True)
print('EVIDENCE',out)
