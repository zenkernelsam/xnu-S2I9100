#!/usr/bin/env python3
"""Production COW installer/ring and original-versus-probed Thumb ABI."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess,struct,json
repo=Path(__file__).resolve().parents[1];root=repo.parent
out=repo/'out'/('hid-execution-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
body='\n'.join(l for l in (repo/'bsd/dev/arm/i9100_hid_execution_trace.h').read_text().splitlines() if not l.startswith('#include'))
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32,vm_map_offset_t;typedef uint64_t user_addr_t;
typedef int kern_return_t,boolean_t;
struct task {int acts;};typedef struct task*task_t;
struct proc {const char*p_comm;unsigned p_pid;task_t task;unsigned char p_uuid[16];unsigned p_idversion;};typedef struct proc*proc_t;
struct map {int dummy;};typedef struct map*vm_map_t;
static struct task child={1},parent={1};static task_t current=&child;static struct map map;static struct proc p;
static unsigned enabled=1,abi=6,irq,interrupts=1,preempt,ops,fail_at,fail_again,reads,allocated,removed,copy_fail_addr;
static uint8_t shared[0x60000],image[0x60000],codebuf[4096],event[136],gs[116];static uint32_t saved[16];
#define FALSE 0
#define TRUE 1
#define KERN_SUCCESS 0
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_COPY 16
#define VM_PROT_ALL 7
#define VM_FLAGS_ANYWHERE 1
#define VM_INHERIT_COPY 1
#define VM_OBJECT_NULL NULL
#define EIO 5
#define EINVAL 22
static task_t current_task(void){return current;}
static vm_map_t current_map(void){return &map;}
static vm_map_t get_task_map(task_t t){return t==&child?&map:NULL;}
static unsigned get_task_numacts(task_t t){return t->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}
static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char*n,int*out,unsigned bytes){assert(bytes==4);*out=!strcmp(n,"i9100hidtrace")?enabled:abi;return 1;}
static int OSCompareAndSwap(unsigned a,unsigned b,volatile unsigned*out){if(*out!=a)return 0;*out=b;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int failing(void){ops++;return ops==fail_at||ops==fail_again;}
static int copyin(user_addr_t a,void*out,unsigned n){
 assert(current==&child&&!irq&&interrupts&&!preempt);reads++;if(a==copy_fail_addr)return 14;
 if(a>=0x65000&&a+n<=0xc5000)memcpy(out,image+(a-0x65000),n);
 else if(a==0x30000&&n==64)memcpy(out,saved,n);
 else if(a==0x20000&&n<=136)memcpy(out,event,n);
 else if(a==0x40000&&n<=116)memcpy(out,gs,n);
 else return 14;return 0;
}
static int vm_map_enter(vm_map_t m,vm_map_offset_t*a,unsigned n,unsigned mask,int flags,void*object,uint64_t offset,int copy,int cur,int max,int inherit){
 assert(m==&map&&n==4096&&mask==0&&flags==1&&object==NULL&&offset==0&&!copy&&cur==3&&max==7&&inherit==1);
 if(failing())return 4;*a=0x1000;allocated=1;return 0;
}
static int vm_map_protect(vm_map_t m,unsigned a,unsigned end,unsigned prot,int max){assert(m==&map&&end==a+4096&&!max&&(prot==5||prot==19));return failing()?4:0;}
static int copyout(const void*in,user_addr_t a,unsigned n){
 if(failing())return 14;
 if(a==0x1000&&n==192){assert(allocated);memcpy(codebuf,in,n);}
 else {assert(a>=0x65000&&a+n<=0xc5000&&n==8);memcpy(image+(a-0x65000),in,n);}return 0;
}
static int vm_map_remove(vm_map_t m,unsigned a,unsigned end,int flags){assert(m==&map&&a==0x1000&&end==0x2000&&!flags);removed=1;allocated=0;return 0;}
static void flush_dcache(unsigned a,unsigned n,int physical){assert(!physical&&(n==8||n==192));}
static void invalidate_icache(unsigned a,unsigned n,int physical){assert(!physical&&(n==8||n==192));}
'''+body+r'''
static void reset(void){
 memset((void*)i9100_hid_execution_diag,0,sizeof(i9100_hid_execution_diag));i9100_hid_execution_diag[0]=1;
 memset((void*)i9100_hid_execution_rows,0,sizeof(i9100_hid_execution_rows));memset(i9100_hid_execution_pending,0,16);i9100_hid_execution_guard=0;
 current=&child;child.acts=1;enabled=interrupts=1;abi=6;irq=preempt=ops=fail_at=fail_again=reads=allocated=removed=copy_fail_addr=0;
 p.p_comm="backboardd";p.p_pid=8;p.task=&child;p.p_idversion=8;memcpy(p.p_uuid,i9100_hid_execution_uuid,16);
 memset(shared,0,sizeof(shared));uint32_t h[7]={0xfeedface,12,9,2};memcpy(shared,h,28);
 for(unsigned n=0;n<3;n++)memcpy(shared+i9100_hid_execution_sites[n]-4096,i9100_hid_execution_original[n],8);
 memcpy(image,shared,sizeof(image));memset(saved,0,64);saved[0]=0xb80f0000;
 memset(event,0,136);uint32_t sz=104,type=11;memcpy(event+32,&sz,4);memcpy(event+36,&type,4);
 memset(gs,0,116);
}
static void install(void){i9100_hid_execution_record(&p,0x65000);assert(!i9100_hid_execution_bootstrap(&p));assert(i9100_hid_execution_diag[1]==2);}
int main(int argc,char**argv){
 assert(argc==2);reset();current=&parent;i9100_hid_execution_record(&p,0x65000);assert(!reads&&!ops&&!i9100_hid_execution_bootstrap(&p));current=&child;
 assert(!i9100_hid_execution_bootstrap(&p)&&i9100_hid_execution_diag[1]==2);
 assert(!memcmp(shared+0x5094,i9100_hid_execution_original[0],8));
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite(codebuf,1,192,f);fclose(f);
 unsigned seen=ops;assert(!i9100_hid_execution_bootstrap(&p)&&ops==seen);
 for(unsigned n=1;n<=12;n++){
  reset();i9100_hid_execution_record(&p,0x65000);fail_at=n;assert(!i9100_hid_execution_bootstrap(&p));
  assert(i9100_hid_execution_diag[1]==3&&!memcmp(image,shared,sizeof(image)));if(n>1)assert(removed);
 }
 reset();i9100_hid_execution_record(&p,0x65000);fail_at=6;fail_again=7;assert(i9100_hid_execution_bootstrap(&p)==EIO&&i9100_hid_execution_diag[9]);
 for(unsigned n=0;n<8;n++){
  reset();i9100_hid_execution_record(&p,0x65000);
  if(n==0)enabled=0;if(n==1)abi=7;if(n==2)irq=1;if(n==3)interrupts=0;if(n==4)preempt=1;if(n==5)child.acts=2;if(n==6)p.p_idversion++;if(n==7)current=&parent;
  assert(!i9100_hid_execution_bootstrap(&p)&&!ops&&!reads);
 }
 reset();p.p_uuid[0]^=1;i9100_hid_execution_record(&p,0x65000);assert(!i9100_hid_execution_pending[0]);
 reset();i9100_hid_execution_record(&p,0x65000);image[0x5094]^=1;assert(!i9100_hid_execution_bootstrap(&p)&&!ops);
 reset();install();
 for(unsigned phase=1;phase<=3;phase++){
  memset(saved,0,64);saved[0]=0xb80f0000;saved[2]=phase==3?0x40000:0x20000;saved[4]=0x20000;saved[5]=0x55;
  i9100_hid_execution_note(&p,0x1000+(phase-1)*64+22,0x30000,phase);
  volatile uint32_t*r=&i9100_hid_execution_rows[4+(phase-1)*64];assert(r[0]==phase&&r[4]==phase&&!r[7]&&!r[13]&&r[14]==(phase==3?116:136));
  if(phase<3)assert(r[24]==104&&r[25]==11);
 }assert(i9100_hid_execution_diag[16]==1&&i9100_hid_execution_diag[17]==1&&i9100_hid_execution_diag[18]==1);
 unsigned count=i9100_hid_execution_rows[0];i9100_hid_execution_note(&p,0x1018,0x30000,1);assert(i9100_hid_execution_rows[0]==count);
 copy_fail_addr=0x30000;i9100_hid_execution_note(&p,0x1016,0x30000,1);assert(i9100_hid_execution_rows[4+3*64+7]==14);copy_fail_addr=0;
 saved[2]=0x20000;for(unsigned n=0;n<40;n++)i9100_hid_execution_note(&p,0x1056,0x30000,2);
 assert(i9100_hid_execution_rows[0]==44&&i9100_hid_execution_diag[17]==41);
 char path[1024];snprintf(path,sizeof(path),"%s.diag",argv[1]);f=fopen(path,"wb");assert(f);fwrite((const void*)i9100_hid_execution_diag,1,sizeof(i9100_hid_execution_diag),f);fclose(f);
 snprintf(path,sizeof(path),"%s.rows",argv[1]);f=fopen(path,"wb");assert(f);fwrite((const void*)i9100_hid_execution_rows,1,sizeof(i9100_hid_execution_rows),f);fclose(f);
 puts("PASS production three-site private COW/parent-child identity, original-byte refusal, all install failure rollbacks, unsafe stop, actual-parameter ring/faults/wrap");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check'),str(out/'code.bin')],check=True)
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *
regs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_CPSR]
sites=[0x6a094,0x70f08,0x6f818];original=[bytes.fromhex(s) for s in ('80b56f4618461146','f0b503af2de9000d','f0b503af81b00446')];code=(out/'code.bin').read_bytes()
report=[]
for n,site in enumerate(sites):
 results=[]
 for traced in (False,True):
  u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);u.mem_map(site&~4095,4096);u.mem_map(0x1000,4096);u.mem_map(0x20000000,0x20000)
  u.mem_write(site,struct.pack('<2I',0xf000f8df,0x1000+n*64+1) if traced else original[n]);u.mem_write(0x1000,code)
  u.reg_write(UC_ARM_REG_CPSR,0xb80f0030)
  for i,r in enumerate(regs[:13]):u.reg_write(r,0x7000+i)
  u.reg_write(UC_ARM_REG_SP,0x20010000);u.reg_write(UC_ARM_REG_LR,0x2001ff01)
  calls=[]
  def hook(uc,at,size,arg):
   if at==site+8:uc.emu_stop()
   if traced and at==0x1000+n*64+20:
    assert uc.reg_read(UC_ARM_REG_R0)==n+1 and uc.reg_read(UC_ARM_REG_R12)==20
    saved=struct.unpack('<16I',uc.mem_read(uc.reg_read(UC_ARM_REG_SP),64));assert list(saved[2:15])==[0x7000+i for i in range(13)]
    calls.append(at);uc.reg_write(UC_ARM_REG_R0,8);uc.reg_write(UC_ARM_REG_R1,1);uc.reg_write(UC_ARM_REG_CPSR,uc.reg_read(UC_ARM_REG_CPSR)^(1<<29));uc.reg_write(UC_ARM_REG_PC,(at+2)|1)
  u.hook_add(UC_HOOK_CODE,hook);u.emu_start(site|1,site+0x100,count=1000)
  values=[u.reg_read(r) for r in regs];sp=values[13];stack=bytes(u.mem_read(sp,0x20010000-sp))
  # GS prologue reserves four uninitialized local bytes. Only compare the
  # register saves; caller stack scratch is not an ABI-defined value.
  if n==2:stack=stack[4:]
  results.append((values,stack))
  assert len(calls)==int(traced)
 assert results[0]==results[1],(n,results)
 report.append(dict(phase=n+1,identical_registers_flags_stack=True))
(out/'result.json').write_text(json.dumps(dict(result='PASS',native_cortex_a9=report),indent=2))
print('PASS Cortex-A9 original/probed callback, digitizer and GS prologues have identical registers/flags/active stack after a real-style getpid return')
print('EVIDENCE',out)
