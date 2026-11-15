#!/usr/bin/env python3
"""Exercise production own-bootstrap COW policy and actual Thumb methods."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import subprocess,json
repo=Path(__file__).resolve().parents[1];root=repo.parent
out=repo/'out'/('quiet-charge-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
body='\n'.join(l for l in (repo/'bsd/dev/arm/i9100_quiet_charge_feedback.h').read_text().splitlines() if not l.startswith('#include'))
source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t UInt32,vm_map_offset_t,vm_offset_t;
typedef uint64_t user_addr_t;typedef int kern_return_t,boolean_t;
struct task {unsigned acts;};typedef struct task*task_t;
struct proc {const char*p_comm;unsigned p_pid;task_t task;unsigned char p_uuid[16];unsigned p_idversion;};typedef struct proc*proc_t;
struct map {unsigned prot;};typedef struct map*vm_map_t;
static struct task child={1},parent={1};static task_t current=&child;
static struct map map={5};static struct proc proc;
static unsigned enabled=1,abi=6,irq,interrupts=1,preempt,ops,fail_at,fail_again,reads,private_copy;
static unsigned char original_page[8],private_page[8],caller[20];
static uint32_t mh[7]={0xfeedface,12,9,2,16,1000,0};
#define FALSE 0
#define TRUE 1
#define KERN_SUCCESS 0
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_COPY 16
#define EIO 5
static task_t current_task(void){return current;}
static vm_map_t current_map(void){return &map;}
static vm_map_t get_task_map(task_t t){return t==&child?&map:NULL;}
static unsigned get_task_numacts(task_t t){return t->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}
static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char*n,int*p,unsigned size){assert(size==4);*p=!strcmp(n,"i9100quietcharge")?enabled:abi;return 1;}
static int OSCompareAndSwap(unsigned a,unsigned b,volatile unsigned*p){if(*p!=a)return 0;*p=b;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int copyin(user_addr_t at,void*out,unsigned n){
 assert(current==&child&&interrupts&&!irq&&!preempt);reads++;
 if(at==0x65000&&n==28)memcpy(out,mh,n);
 else if(at==0x75958&&n==8)memcpy(out,original_page,n);
 else if(at==0x7cef8&&n==20)memcpy(out,caller,n);
 else return 14;return 0;
}
static int failing(void){ops++;return ops==fail_at||ops==fail_again;}
static int vm_map_protect(vm_map_t m,unsigned start,unsigned end,unsigned prot,int max){
 assert(m==&map&&start==0x75000&&end==0x76000&&!max);
 if(failing())return 4;
 if(prot&VM_PROT_COPY){assert(prot==(1|2|16));if(!private_copy)memcpy(private_page,original_page,8);private_copy=1;map.prot=3;}
 else {assert(prot==5);map.prot=5;}return 0;
}
static int copyout(const void*in,user_addr_t at,unsigned n){
 assert(current==&child&&at==0x75958&&n==8&&private_copy&&map.prot==3);
 if(failing()){memcpy(private_page,in,2);return 14;}memcpy(private_page,in,8);return 0;
}
static void flush_dcache(unsigned at,unsigned n,int physical){assert(at==0x75958&&n==8&&!physical);}
static void invalidate_icache(unsigned at,unsigned n,int physical){assert(at==0x75958&&n==8&&!physical);}
'''+body+r'''
static void reset(void){
 memset((void*)i9100_quiet_charge_diag,0,sizeof(i9100_quiet_charge_diag));i9100_quiet_charge_diag[0]=1;
 i9100_quiet_charge_guard=0;memset(i9100_quiet_charge_pending,0,sizeof(i9100_quiet_charge_pending));
 current=&child;child.acts=1;map.prot=5;enabled=interrupts=1;abi=6;irq=preempt=ops=fail_at=fail_again=reads=private_copy=0;
 proc.p_comm="SpringBoard";proc.p_pid=32;proc.task=&child;proc.p_idversion=1;memcpy(proc.p_uuid,i9100_quiet_charge_uuid,16);
 memcpy(original_page,i9100_quiet_charge_original,8);memcpy(caller,i9100_quiet_charge_caller,20);mh[0]=0xfeedface;
}
int main(void){
 reset();current=&parent;i9100_quiet_charge_record(&proc,0x65000);assert(!reads&&!ops);
 assert(!i9100_quiet_charge_bootstrap(&proc)&&!reads&&!ops);
 current=&child;assert(!i9100_quiet_charge_bootstrap(&proc));assert(i9100_quiet_charge_diag[1]==2&&i9100_quiet_charge_diag[12]==1&&map.prot==5);
 assert(!memcmp(private_page,i9100_quiet_charge_return,8)&&!memcmp(original_page,i9100_quiet_charge_original,8));
 unsigned seen=ops;assert(!i9100_quiet_charge_bootstrap(&proc)&&ops==seen);
 for(unsigned n=0;n<10;n++){
  reset();i9100_quiet_charge_record(&proc,0x65000);
  if(n==0)enabled=0;if(n==1)abi=7;if(n==2)irq=1;if(n==3)interrupts=0;if(n==4)preempt=1;
  if(n==5)child.acts=2;if(n==6)proc.p_idversion=2;if(n==7)proc.p_pid=33;
  if(n==8)proc.p_comm="Other";if(n==9)current=&parent;
  assert(!i9100_quiet_charge_bootstrap(&proc)&&!ops&&!reads);
 }
 reset();proc.p_uuid[0]^=1;i9100_quiet_charge_record(&proc,0x65000);assert(!i9100_quiet_charge_pending[0]&&!i9100_quiet_charge_bootstrap(&proc));
 reset();i9100_quiet_charge_record(&proc,0x65001);assert(!i9100_quiet_charge_pending[0]);
 for(unsigned n=0;n<3;n++){
  reset();i9100_quiet_charge_record(&proc,0x65000);if(n==0)mh[0]=0;if(n==1)original_page[0]^=1;if(n==2)caller[0]^=1;
  assert(!i9100_quiet_charge_bootstrap(&proc)&&!ops&&i9100_quiet_charge_diag[1]==3);
 }
 for(unsigned n=1;n<=3;n++){
  reset();i9100_quiet_charge_record(&proc,0x65000);fail_at=n;assert(!i9100_quiet_charge_bootstrap(&proc));
  assert(i9100_quiet_charge_diag[1]==3&&!i9100_quiet_charge_diag[12]&&map.prot==5);
  assert(!memcmp(original_page,i9100_quiet_charge_original,8));if(private_copy)assert(!memcmp(private_page,original_page,8));
 }
 reset();i9100_quiet_charge_record(&proc,0x65000);fail_at=2;fail_again=4;assert(i9100_quiet_charge_bootstrap(&proc)==EIO&&i9100_quiet_charge_diag[9]);
 reset();i9100_quiet_charge_record(&proc,0x65000);fail_at=3;fail_again=4;assert(i9100_quiet_charge_bootstrap(&proc)==EIO&&i9100_quiet_charge_diag[9]);
 reset();for(unsigned n=0;n<9;n++){
  proc.p_pid=32+n;proc.p_idversion=1+n;private_copy=0;map.prot=5;i9100_quiet_charge_record(&proc,0x65000);assert(!i9100_quiet_charge_bootstrap(&proc));
  if(n<8)assert(private_copy);else assert(!private_copy);
 }assert(i9100_quiet_charge_diag[6]==8&&i9100_quiet_charge_diag[12]==8);
 puts("PASS production parent/child handoff, own single-thread bootstrap, exact image/caller guards, private COW, rollback and unsafe-stop, eight-install cap");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_SP,UC_ARM_REG_LR
saved=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
results=[]
for quiet in (False,True):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.mem_map(0x11000,4096);u.mem_map(0x1b0000,4096);u.mem_map(0x20008000,4096)
 code=bytes.fromhex('704700bf00bf00bf' if quiet else '40f252409ef1e2be');u.mem_write(0x11958,code)
 u.reg_write(UC_ARM_REG_R0,0x12345678);u.reg_write(UC_ARM_REG_R1,0x87654321);u.reg_write(UC_ARM_REG_SP,0x20008000);u.reg_write(UC_ARM_REG_LR,0x20008001)
 for n,r in enumerate(saved):u.reg_write(r,0x5000+n)
 reached=[]
 def hook(uc,at,size,arg):
  if at in (0x1b0724,0x20008000):reached.append(at);uc.emu_stop()
 u.hook_add(UC_HOOK_CODE,hook);u.emu_start(0x11959,0x20008ffc,count=100)
 assert reached==([0x20008000] if quiet else [0x1b0724])
 assert u.reg_read(UC_ARM_REG_R0)==(0x12345678 if quiet else 1106)
 assert all(u.reg_read(r)==0x5000+n for n,r in enumerate(saved))
 assert u.reg_read(UC_ARM_REG_SP)==0x20008000 and u.reg_read(UC_ARM_REG_LR)==0x20008001
 results.append(dict(quiet=quiet,reached=hex(reached[0])))
(out/'result.json').write_text(json.dumps(dict(result='PASS',native_thumb=results,physical_audio_tested=False),indent=2))
print('PASS actual Thumb charging method reaches sound1106; explicit quiet variant returns void without calling sound; saved registers/stack preserved')
print('EVIDENCE',out)
