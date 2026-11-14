#!/usr/bin/env python3
"""Exercise native Fig RPC observation gates, ownership and final-copy errors.

The production C functions execute with modeled IPC/atomics/user reads. Native
RPC results remain unchanged; this is not a full system or phone validation.
"""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('fig-rpc-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
text=(repo/'osfmk/ipc/ipc_kmsg.c').read_text()
def function(name):
    kind='static unsigned ' if name.endswith('begin') else 'static void '
    start=text.index(kind+name+'(');brace=text.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint32_t UInt32;
static int OSCompareAndSwap(unsigned old,unsigned value,volatile unsigned*p){if(*p!=old)return 0;*p=value;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
#define I9100_FIG_RPC_IMPLEMENTATION 1
'''+f'#include "{repo}/pexpert/arm/i9100_fig_rpc_diag.h"\n'+r'''
typedef uint32_t mach_msg_size_t,mach_msg_return_t;typedef uint64_t mach_vm_address_t;
typedef struct {uint32_t bits,size,remote,local,reserved;int32_t msgh_id;} mach_msg_header_t;
#define msgh_remote_port remote
struct Message {unsigned size;mach_msg_header_t *header;};typedef struct Message *ipc_kmsg_t;
#define ikm_header header
#define ikm_size size
static unsigned kernel_task=1,task=2,pid=33,thread=0x1234,irq,interrupts=1,preemption,enable=1,reads,read_error;
static const char *process="mediaserverd";static unsigned char user[128];
static unsigned current_task(void){return task;}static unsigned current_thread(void){return thread;}
static unsigned proc_selfpid(void){return pid;}static void proc_name(unsigned id,char*out,unsigned n){assert(id==pid);strncpy(out,process,n);}
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,int*p,unsigned z){assert(!strcmp(n,"i9100xpcdiag")&&z==4);*p=enable;return 1;}
static unsigned copyin(mach_vm_address_t a,void*out,unsigned n){reads++;assert(a==0x12000&&n<=128);if(read_error)return read_error;memcpy(out,user,n);return 0;}
'''+function('i9100_fig_rpc_put_begin')+'\n'+function('i9100_fig_rpc_put_end')+r'''
static volatile uint32_t *row(unsigned n){return &i9100_fig_rpc_diag[1+(n-1)*56];}
int main(int argc,char**argv){
 assert(argc==2);unsigned char raw[128]={0};mach_msg_header_t*h=(void*)raw;h->bits=0x1513;h->size=24;h->msgh_id=9393;h->remote=0x2b03;
 unsigned q=i9100_fig_rpc_record(1,raw,128,24,pid,thread,task,process,1,0x113,0,0,0x2b03,~0U,~0U,0);assert(q==1&&!memcmp((const void*)&row(q)[24],raw,24));
 i9100_fig_rpc_finish(q,pid+1,thread,14);assert(!row(q)[15]);
 i9100_fig_rpc_finish(q,pid,thread+4,14);assert(!row(q)[15]);
 i9100_fig_rpc_finish(q,pid,thread,0);assert(row(q)[15]&&!row(q)[16]);
 q=i9100_fig_rpc_record(2,raw,128,24,pid,thread,task,process,1,0x113,0,0,0xc1234000,~0U,~0U,0);assert(q==2&&row(q)[19]==q);
 i9100_fig_rpc_finish(q,pid,thread,0x10000004);assert(row(q)[16]==0x10000004);
 i9100_fig_rpc_receive_done(pid+1,thread,0);assert(!row(q)[17]);
 i9100_fig_rpc_receive_done(pid,thread,0x10004003);assert(row(q)[17]&&row(q)[18]==0x10004003);
 i9100_fig_rpc_receive_done(pid,thread,0);assert(row(q)[18]==0x10004003);
 for(unsigned n=0;n<9;n++){
  unsigned enabled=1,size=24,available=128,stage=1,id=9393,p=pid,t=thread,owner=task;const char*name=process;
  switch(n){case 0:enabled=0;break;case 1:size=129;break;case 2:available=23;break;case 3:stage=0;break;case 4:stage=7;break;case 5:id=9394;break;case 6:name="unrelated";break;case 7:p=0;break;case 8:owner=0;break;}
  h->msgh_id=id;assert(!i9100_fig_rpc_record(stage,raw,available,size,p,t,owner,name,enabled,0,0,0,0,0,0,0));
 }
 h->msgh_id=9393;struct {struct Message message;unsigned char payload[128];} storage={0};
 storage.message.size=128;storage.message.header=(void*)storage.payload;memcpy(storage.payload,raw,128);
 for(unsigned n=0;n<6;n++){
  task=2;irq=0;interrupts=1;preemption=0;enable=1;
  switch(n){case 0:task=kernel_task;break;case 1:irq=1;break;case 2:interrupts=0;break;case 3:preemption=1;break;case 4:enable=0;break;case 5:process="unrelated";break;}
  assert(!i9100_fig_rpc_put_begin(&storage.message,0x12000,32));
 }
 task=2;irq=0;interrupts=1;preemption=0;enable=1;process="mediaserverd";
 assert(!i9100_fig_rpc_put_begin(&storage.message,0x3fffffffULL,32));
 storage.message.size=31;assert(!i9100_fig_rpc_put_begin(&storage.message,0x12000,32));storage.message.size=128;
 q=i9100_fig_rpc_put_begin(&storage.message,0x12000,32);assert(q==3);memcpy(user,raw,128);
 i9100_fig_rpc_put_end(q,&storage.message,0x12000,32,0);assert(reads==1&&i9100_fig_rpc_diag[0]==4&&row(4)[2]==6);
 q=i9100_fig_rpc_put_begin(&storage.message,0x12000,32);read_error=14;
 i9100_fig_rpc_put_end(q,&storage.message,0x12000,32,0);assert(reads==2&&row(6)[12]==14);
 q=i9100_fig_rpc_put_begin(&storage.message,0x12000,32);
 i9100_fig_rpc_put_end(q,&storage.message,0x12000,32,0x1000400f);assert(reads==2&&row(q)[16]==0x1000400f);
 h->msgh_id=9493;
 q=i9100_fig_rpc_record(1,raw,128,48,58,0x4567,3,"SpringBoard",1,3,0,0,0,0,0,0);assert(q==8&&row(q)[6]==2);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite((const void*)i9100_fig_rpc_diag,1,sizeof(i9100_fig_rpc_diag),f);fclose(f);
 for(unsigned n=0;n<80;n++)i9100_fig_rpc_record(1,raw,128,48,pid,thread,task,process,1,3,0,0,0,0,0,0);
 assert(i9100_fig_rpc_diag[0]==64&&row(1)[7]==9393&&row(2)[18]==0x10004003);
 puts("PASS production Fig RPC scope, owner/timeout, complete copy/read-error, first64 retention; IPC/atomics modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'fig-rpc.bin')],check=True)
print('EVIDENCE',out)
