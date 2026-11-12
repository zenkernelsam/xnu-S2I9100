#!/usr/bin/env python3
"""Exercise actual query observer and final-copy wrappers; IPC/atomics modeled."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('ls-query-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
text=(repo/'osfmk/ipc/ipc_kmsg.c').read_text()
def function(name):
    start=text.index('static unsigned '+name+'(') if name.endswith('begin') else text.index('static void '+name+'(')
    brace=text.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
source=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t UInt32,mach_msg_size_t,mach_msg_return_t;typedef uint64_t mach_vm_address_t;
static int OSCompareAndSwap(unsigned old,unsigned next,volatile unsigned*p){if(*p!=old)return 0;*p=next;return 1;}
#define OSMemoryBarrier() ((void)0)
#define I9100_LS_QUERY_IMPLEMENTATION 1
'''+f'#include "{repo}/pexpert/arm/i9100_ls_query_diag.h"\n'+r'''
typedef struct {uint32_t bits,size,remote,local,reserved;int32_t msgh_id;} mach_msg_header_t;
struct Message {unsigned size;mach_msg_header_t *header;};
typedef struct Message *ipc_kmsg_t;
#define ikm_header header
#define ikm_size size
static unsigned kernel_task=1,task=2,pid=38,thread=0x1234,irq,interrupts=1,preemption,enable=1,reads,read_error;
static const char *process="locationd";static unsigned char user[352];
static unsigned current_task(void){return task;}static unsigned current_thread(void){return thread;}
static unsigned proc_selfpid(void){return pid;}static void proc_name(unsigned id,char*out,unsigned n){assert(id==pid);strncpy(out,process,n);}
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preemption;}
static int PE_parse_boot_argn(const char*n,int*p,unsigned z){assert(!strcmp(n,"i9100xpcdiag") && z==4);*p=enable;return 1;}
static unsigned copyin(mach_vm_address_t address,void*dst,unsigned n){reads++;assert(address==0x12000 && n<=352);if(read_error)return read_error;memcpy(dst,user,n);return 0;}
'''+function('i9100_ls_query_put_begin')+'\n'+function('i9100_ls_query_put_end')+r'''
int main(void){
 unsigned char raw[352]={0};mach_msg_header_t *h=(void*)raw;h->bits=0x1513;h->size=288;h->msgh_id=14012;strcpy((char*)raw+32,"com.apple.locationd");
 unsigned token=i9100_ls_query_record(1,raw,352,288,38,thread,process,1,~0U,~0U,3,0,0,0);
 assert(token==1 && !memcmp((const void*)&i9100_ls_query_diag[1+16],raw,288));
 i9100_ls_query_finish(token,39,thread,17);assert(!i9100_ls_query_diag[1+12]);
 i9100_ls_query_finish(token,38,thread+4,17);assert(!i9100_ls_query_diag[1+12]);
 i9100_ls_query_finish(token,38,thread,0x1000000c);assert(i9100_ls_query_diag[1+12] && i9100_ls_query_diag[1+13]==0x1000000c);
 i9100_ls_query_finish(token,38,thread,0);assert(i9100_ls_query_diag[1+13]==0x1000000c);
 for(unsigned n=0;n<10;n++){
  unsigned enabled=1,size=288,available=352,phase=1,id=14012,owner=38;const char *name=process;
  switch(n){case 0:enabled=0;break;case 1:size=353;break;case 2:available=287;break;case 3:phase=0;break;
   case 4:phase=7;break;case 5:id=14014;break;case 6:name="installd";break;case 7:owner=0;break;
   case 8:size=23;break;case 9:available=23;break;}
  h->msgh_id=id;assert(!i9100_ls_query_record(phase,raw,available,size,owner,thread,name,enabled,0,0,3,0,0,0));
 }
 h->msgh_id=14012;
 struct {struct Message message;unsigned char payload[352];} storage={0};
 storage.message.size=352;storage.message.header=(void*)storage.payload;memcpy(storage.payload,raw,352);
 for(unsigned n=0;n<6;n++){
  task=2;irq=0;interrupts=1;preemption=0;enable=1;
  switch(n){case 0:task=kernel_task;break;case 1:irq=1;break;case 2:interrupts=0;break;case 3:preemption=1;break;case 4:enable=0;break;case 5:process="unrelated";break;}
  assert(!i9100_ls_query_put_begin(&storage.message,0x12000,340));
 }
 task=2;irq=0;interrupts=1;preemption=0;enable=1;process="locationd";
 assert(!i9100_ls_query_put_begin(&storage.message,0x3fffff80ULL,340));
 storage.message.size=339;assert(!i9100_ls_query_put_begin(&storage.message,0x12000,340));storage.message.size=352;
 token=i9100_ls_query_put_begin(&storage.message,0x12000,340);assert(token==2);memcpy(user,raw,352);
 i9100_ls_query_put_end(token,&storage.message,0x12000,340,0);assert(reads==1 && i9100_ls_query_diag[0]==3);
 assert(i9100_ls_query_diag[1+2*104+3]==6 && !i9100_ls_query_diag[1+2*104+9]);
 token=i9100_ls_query_put_begin(&storage.message,0x12000,340);h=(void*)user;h->msgh_id=0xdeadbeef;
 i9100_ls_query_put_end(token,&storage.message,0x12000,340,0);assert(reads==2 && i9100_ls_query_diag[0]==5);
 assert(i9100_ls_query_diag[1+4*104+4]==14012 && i9100_ls_query_diag[1+4*104+16+5]==0xdeadbeef);
 token=i9100_ls_query_put_begin(&storage.message,0x12000,340);read_error=14;
 i9100_ls_query_put_end(token,&storage.message,0x12000,340,0);assert(reads==3 && i9100_ls_query_diag[0]==7);
 assert(i9100_ls_query_diag[1+6*104+9]==14);
 token=i9100_ls_query_put_begin(&storage.message,0x12000,340);i9100_ls_query_put_end(token,&storage.message,0x12000,340,0x1000400f);
 assert(reads==3 && i9100_ls_query_diag[0]==8 && i9100_ls_query_diag[1+7*104+13]==0x1000400f);
 i9100_ls_query_allocation(67,thread,1,0x12000,4160,0,0x14000001,3,1,0,0);
 for(unsigned n=0;n<70;n++)i9100_ls_query_allocation(67,thread,1,0x12000,4160,0,0x14000001,0,1,0,0x35000+n*4096);
 assert(i9100_ls_query_alloc_diag[0]==71 && i9100_ls_query_alloc_failure_diag[0]==1 && i9100_ls_query_alloc_failure_diag[1+8]==3);
 for(unsigned n=0;n<10;n++)i9100_ls_query_allocation(67,thread,1,0x12000,4160,0,1,14,1,0,0);
 assert(i9100_ls_query_alloc_failure_diag[0]==8);
 FILE*f=fopen("query.bin","wb");assert(f);fwrite((const void*)i9100_ls_query_diag,1,sizeof(i9100_ls_query_diag),f);fclose(f);
 f=fopen("alloc.bin","wb");assert(f);fwrite((const void*)i9100_ls_query_alloc_diag,1,sizeof(i9100_ls_query_alloc_diag),f);fclose(f);
 f=fopen("failures.bin","wb");assert(f);fwrite((const void*)i9100_ls_query_alloc_failure_diag,1,sizeof(i9100_ls_query_alloc_failure_diag),f);fclose(f);
 for(unsigned n=0;n<40;n++)i9100_ls_query_record(1,raw,352,288,38,thread,process,1,0,0,3,0,0,0);
 assert(i9100_ls_query_diag[0]==48);i9100_ls_query_finish(1,38,thread,0);assert(i9100_ls_query_diag[1+13]!=0x1000000c);
 puts("PASS production query/final-copy bounds and ownership, observed mutated/read-error data, rolling allocation with retained failures; IPC/atomics modeled");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True);print('EVIDENCE',out)
