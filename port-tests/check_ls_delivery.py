#!/usr/bin/env python3
"""Exercise the production final-message snapshot and exact allocator note.

Self-map copyin/copyout, allocation backend and atomic scheduling are
modeled. Only observer bounds/ownership and original results are checked.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
import subprocess
from datetime import datetime
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('ls-delivery-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
text=(repo/'osfmk/ipc/mach_kernelrpc.c').read_text()
begin=text.index('static void i9100_ls_alloc_note(');brace=text.index('{',begin);end=brace+1;depth=1
while depth:
    depth+=(text[end]=='{')-(text[end]=='}');end+=1
func=text[begin:end]
source=r'''
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t UInt32;typedef uint64_t user_addr_t;
static int OSCompareAndSwap(unsigned old,unsigned next,volatile unsigned*p){if(*p!=old)return 0;*p=next;return 1;}
#define OSMemoryBarrier() ((void)0)
static unsigned char user[1664];static unsigned copies,read_error;
static int copyin(user_addr_t address,void*dst,unsigned n){
 copies++;if(read_error)return 14;assert(address==0x12000 && n<=sizeof(user));memcpy(dst,user,n);return 0;
}
'''+f'#include "{repo}/pexpert/arm/i9100_ls_delivery.h"\n'+r'''
struct j1_vm_allocate_wire {uint32_t target,addr,size_low,size_high,flags;};
volatile uint32_t i9100_ls_reply_alloc_diag[1+32*12];
static unsigned enable=1;static const char *process_name="lsd";
static int PE_parse_boot_argn(const char*n,int*p,unsigned s){(void)n;assert(s==4);*p=enable;return 1;}
static int proc_selfpid(void){return 70;}
static unsigned current_thread(void){return 0x1234;}
static void proc_name(int pid,char*out,int n){(void)pid;strncpy(out,process_name,n);}
'''+func+r'''
int main(void){
 unsigned char source[1664]={0};uint32_t id=14008;memcpy(source+20,&id,4);source[42]=0x41;
 unsigned token=i9100_ls_delivery_begin(source,sizeof(source),1400,0x12000,70,0x1234,0x80001200,"lsd",1,1,58);
 assert(token==1);memcpy(user,source,sizeof(user));
 i9100_ls_delivery_end(token,71,0x1234,0x80001200,0,1);assert(!copies);
 i9100_ls_delivery_end(token,70,0x1235,0x80001200,0,1);assert(!copies);
 i9100_ls_delivery_end(token,70,0x1234,0x80001200,0,1);
 assert(copies==1 && i9100_ls_delivery_diag[1]==1 && i9100_ls_delivery_diag[1+9]==1);
 assert(!i9100_ls_delivery_diag[1+11] && i9100_ls_delivery_diag[1+12]==58);
 i9100_ls_delivery_end(token,70,0x1234,0x80001200,0,1);assert(copies==1);
 token=i9100_ls_delivery_begin(source,1400,352,0x12000,70,0x1234,0x80001200,"lsd",1,1,58);
 user[42]=0;i9100_ls_delivery_end(token,70,0x1234,0x80001200,0,1);
 assert(i9100_ls_delivery_diag[1+848+11]==1);
 token=i9100_ls_delivery_begin(source,1400,352,0x12000,70,0x1234,0x80001200,"lsd",1,1,58);
 read_error=1;i9100_ls_delivery_end(token,70,0x1234,0x80001200,0,1);
 assert(i9100_ls_delivery_diag[1+2*848+8]==14);
 token=i9100_ls_delivery_begin(source,1400,352,0x12000,70,0x1234,0x80001200,"lsd",1,1,58);
 unsigned prior=copies;i9100_ls_delivery_end(token,70,0x1234,0x80001200,0x1000400f,1);
 assert(copies==prior && i9100_ls_delivery_diag[1+3*848+7]==0x1000400f);
 for(unsigned n=0;n<7;n++){
  assert(!i9100_ls_delivery_begin(source,n==0?24:1400,n==1?1665:352,n==2?0x3fffff80U:0x12000,
       n==3?0:70,0x1234,0x80001200,n==4?"installd":"lsd",n==5?0:1,n==6?0:1,58));
 }
 read_error=0;id=14014;memcpy(source+20,&id,4);strcpy((char*)source+48,"/Applications/AppStore.app.extra");
 assert(!i9100_ls_delivery_begin(source,1664,1664,0x12000,70,0x1234,0x80001200,"lsd",1,1,58));
 strcpy((char*)source+48,"/Applications/AppStore.app");
 token=i9100_ls_delivery_begin(source,1664,1664,0x12000,70,0x1234,0x80001200,"lsd",1,1,58);
 assert(token==5);memcpy(user,source,1664);i9100_ls_delivery_end(token,70,0x1234,0x80001200,0,1);
 assert(i9100_ls_delivery_diag[1+4*848+9]==1);
 FILE*f=fopen("delivery.bin","wb");assert(f);fwrite((const void*)i9100_ls_delivery_diag,1,sizeof(i9100_ls_delivery_diag),f);fclose(f);
 struct j1_vm_allocate_wire w={1,0x12000,0x1040,0,0x14000001};
 i9100_ls_alloc_note(&w,3,1,0,0x23000);
 assert(i9100_ls_reply_alloc_diag[0]==1 && i9100_ls_reply_alloc_diag[1+8]==3 && i9100_ls_reply_alloc_diag[1+9]==1);
 i9100_ls_alloc_note(&w,0,1,0,0x24000);assert(i9100_ls_reply_alloc_diag[0]==2);
 process_name="SpringBoard";i9100_ls_alloc_note(&w,0,1,0,0x24000);assert(i9100_ls_reply_alloc_diag[0]==2);
 process_name="lsd";enable=0;i9100_ls_alloc_note(&w,0,1,0,0x24000);assert(i9100_ls_reply_alloc_diag[0]==2);
 enable=1;w.size_low=8192;i9100_ls_alloc_note(&w,0,1,0,0x24000);assert(i9100_ls_reply_alloc_diag[0]==2);
 f=fopen("allocate.bin","wb");assert(f);fwrite((const void*)i9100_ls_reply_alloc_diag,1,sizeof(i9100_ls_reply_alloc_diag),f);fclose(f);
 puts("PASS production delivery bounds/ownership and real allocator success/failure retention; backends modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True);print('EVIDENCE',out)
