#!/usr/bin/env python3
"""Exercise actual LS file/fd observer, with syscalls and atomics modeled."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('ls-files-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t UInt32;
static int OSCompareAndSwap(unsigned old,unsigned next,volatile unsigned*p){if(*p!=old)return 0;*p=next;return 1;}
#define OSMemoryBarrier() ((void)0)
'''+f'#include "{repo}/pexpert/arm/i9100_ls_files.h"\n'+r'''
int main(void){
 const char *path="/var/mobile/Library/Caches/com.apple.IconsCache/_temp";
 assert(i9100_ls_file_match(path) && i9100_ls_file_match("/var/mobile/Library/Caches"));
 assert(!i9100_ls_file_match("/var/mobile/Library/CachesElsewhere") && !i9100_ls_file_match("/Applications/AppStore.app.extra"));
 i9100_ls_file_publish(69,0x1234,5,0,7,0xffffffff,0x601,0600,0,path,0,0,0,0x123,0x456,1);
 assert(i9100_ls_file_diag[0]==1 && i9100_ls_fds[0].pid==69 && i9100_ls_fds[0].fd==7);
 i9100_ls_file_publish(70,0x1234,4,0,100,7,0,0,100,0,0,0,0,0,0,0);
 assert(i9100_ls_file_diag[0]==1); /* foreign pid cannot borrow the fd */
 i9100_ls_file_publish(69,0x1234,4,28,0,7,0,0,4096,0,0,0,0,0,0,0);
 assert(i9100_ls_file_diag[0]==2 && i9100_ls_file_diag[1+112+4]==28);
 assert(!strcmp((const char*)&i9100_ls_file_diag[1+112+16],path));
 i9100_ls_file_publish(69,0x1234,128,0,0,0,0,0,0,path,"/var/mobile/Library/Caches/com.apple.IconsCache/AppStore",0,0,0,0,0);
 assert(i9100_ls_file_diag[0]==3);
 i9100_ls_file_publish(69,0x1234,6,0,0,7,0,0,0,0,0,0,0,0,0,2);
 assert(i9100_ls_file_diag[0]==4 && !i9100_ls_fds[0].pid);
 i9100_ls_file_publish(69,0x1234,4,0,100,7,0,0,100,0,0,0,0,0,0,0);
 assert(i9100_ls_file_diag[0]==4); /* closed fd cannot acquire old provenance */
 i9100_ls_file_publish(69,0x1234,5,13,0,0,0,0,0,path,0,0,0,0,0,1);
 assert(i9100_ls_file_diag[0]==5 && !i9100_ls_fds[0].pid);
 FILE*f=fopen("files.bin","wb");assert(f);fwrite((const void*)i9100_ls_file_diag,1,sizeof(i9100_ls_file_diag),f);fclose(f);
 for(unsigned n=0;n<80;n++)i9100_ls_file_publish(69,0x1234,136,17,0,0,0,0700,0,path,0,0,0,0,0,0);
 assert(i9100_ls_file_diag[0]==85 && i9100_ls_file_diag[1+((85-1)&63)*112]==85);
 puts("PASS production LS path boundaries, fd provenance/close/error and bounded ring; syscall/atomic backends modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True);print('EVIDENCE',out)
