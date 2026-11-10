#!/usr/bin/env python3
"""Production publish-record bounds and post-send lifetime checks; IPC modeled."""
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1]
text=(repo/'osfmk/ipc/mach_msg.c').read_text()
start=text.index('static unsigned i9100_ls_publish_record(')
functions=text[start:text.index('static void i9100_service_wire_record(',start)]
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UInt32,mach_msg_option_t;typedef int32_t mach_msg_return_t;
enum {MACH_MSG_SUCCESS=0,MACH_MSG_TRAILER_FORMAT_0=0,MAX_TRAILER_SIZE=80};
typedef struct {uint32_t msgh_bits,msgh_size,msgh_remote_port,msgh_local_port,msgh_reserved;int32_t msgh_id;} mach_msg_header_t;
typedef struct {uint32_t msgh_trailer_type,msgh_trailer_size,seqno,security[2];struct{uint32_t val[8];}msgh_audit;} mach_msg_audit_trailer_t;
struct ipc_kmsg {unsigned ikm_size;mach_msg_header_t *ikm_header;};typedef struct ipc_kmsg *ipc_kmsg_t;
static volatile uint32_t i9100_ls_publish_wire_diag[1+16*352],i9100_ls_publish_guard;
static unsigned checks,pid=57,enabled=1,busy;static uintptr_t thread=0x1234;static const char *name="SpringBoard";
static int proc_selfpid(void){return pid;}static void *current_thread(void){return (void*)thread;}
static void proc_name(int p,char *out,unsigned n){(void)p;strncpy(out,name,n-1);out[n-1]=0;}
static int i9100_xpc_boundary_enabled(void){return enabled;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile uint32_t *p){if(busy||*p!=old)return 0;*p=value;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
#define round_msg(n) (((n)+3U)&~3U)
static void check(int ok,const char *why){++checks;if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
struct fixture{struct ipc_kmsg msg;_Alignas(8)unsigned char bytes[2048];};
'''
suffix=r'''
int main(void){
 struct fixture f;memset(&f,0,sizeof(f));f.msg.ikm_size=sizeof(f.bytes);
 mach_msg_header_t *h=(void*)(f.bytes+64);f.msg.ikm_header=h;
 h->msgh_id=14008;h->msgh_size=1336;h->msgh_bits=0x80001513;h->msgh_remote_port=0x5e07;h->msgh_local_port=0x1707;
 unsigned first=i9100_ls_publish_record(1,&f.msg,3,0);
 check(first==1 && i9100_ls_publish_wire_diag[1+6]==1336,"complete actual publish copied");
 check(!memcmp((const void*)&i9100_ls_publish_wire_diag[1+16],h,1336),"payload bytes not reconstructed");
 i9100_ls_publish_finish(first,0,0);check(i9100_ls_publish_wire_diag[1+12]==1 && i9100_ls_publish_wire_diag[1+13]==0,"copyin result published");
 h->msgh_size=1340;unsigned native=i9100_ls_publish_record(2,&f.msg,3,0);
 unsigned at=1+((native-1)&15)*352;check(i9100_ls_publish_wire_diag[at+5]==1340 && i9100_ls_publish_wire_diag[at+12]==1,"native expanded header retained");
 f.msg.ikm_header=(void*)1; /* Sending may consume/free the original kmsg. */
 i9100_ls_publish_finish(native,1,0x10000004);
 check(i9100_ls_publish_wire_diag[at+14]==1 && i9100_ls_publish_wire_diag[at+15]==0x10000004,"send completion needs no consumed message");
 f.msg.ikm_header=h;h->msgh_size=1500;
 unsigned large=i9100_ls_publish_record(1,&f.msg,3,0);unsigned la=1+((large-1)&15)*352;
 check(i9100_ls_publish_wire_diag[la+5]==1500 && i9100_ls_publish_wire_diag[la+6]==1344,"prefix cap retains original size");
 h->msgh_size=4096;check(!i9100_ls_publish_record(1,&f.msg,3,0),"allocation bounds");
 f.msg.ikm_header=(void*)(f.bytes+sizeof(f.bytes)+1);check(!i9100_ls_publish_record(1,&f.msg,3,0),"header pointer bounds before dereference");
 f.msg.ikm_header=h;h->msgh_size=1336;enabled=0;check(!i9100_ls_publish_record(1,&f.msg,3,0),"explicit diagnostic gate");enabled=1;
 h->msgh_id=14007;check(!i9100_ls_publish_record(1,&f.msg,3,0),"ordinary store traffic excluded");h->msgh_id=14008;
 name="Other";check(!i9100_ls_publish_record(1,&f.msg,3,0),"process scope");name="SpringBoard";
 busy=1;check(!i9100_ls_publish_record(1,&f.msg,3,0),"busy producer does not block");busy=0;
 unsigned stale=i9100_ls_publish_record(1,&f.msg,3,0);unsigned sa=1+((stale-1)&15)*352;
 for(unsigned i=0;i<16;i++)(void)i9100_ls_publish_record(1,&f.msg,3,0);
 unsigned owner=i9100_ls_publish_wire_diag[sa];i9100_ls_publish_finish(stale,1,0xbeef);
 check(i9100_ls_publish_wire_diag[sa]==owner && !i9100_ls_publish_wire_diag[sa+14],"stale completion cannot overwrite another record");
 unsigned own=i9100_ls_publish_record(1,&f.msg,3,0);unsigned oa=1+((own-1)&15)*352;
 pid=58;i9100_ls_publish_finish(own,1,0);check(!i9100_ls_publish_wire_diag[oa+14],"PID ownership retained");pid=57;
 thread=0x5678;i9100_ls_publish_finish(own,1,0);check(!i9100_ls_publish_wire_diag[oa+14],"thread ownership retained");thread=0x1234;
 h->msgh_id=14108;h->msgh_size=36;
 mach_msg_audit_trailer_t *t=(void*)((unsigned char*)h+36);memset(t,0,sizeof(*t));t->msgh_trailer_size=sizeof(*t);t->msgh_audit.val[5]=57;t->msgh_audit.val[1]=501;
 unsigned reply=i9100_ls_publish_record(4,&f.msg,2,0);unsigned ra=1+((reply-1)&15)*352;
 check(i9100_ls_publish_wire_diag[ra+10]==57 && i9100_ls_publish_wire_diag[ra+11]==501 && i9100_ls_publish_wire_diag[ra+6]==36+sizeof(*t),"reply audit bytes bounded and attributed");
 printf("%u production publish lifetime/bounds checks passed; IPC boundary modeled\n",checks);
}
'''
out=repo/'out'/('ls-publish-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir(parents=True)
source=out/'publish.c';binary=out/'publish';source.write_text(prefix+functions+suffix)
subprocess.run(['clang','-std=c11','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined',
    '-fno-sanitize-recover=all',str(source),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
print(out,flush=True)
