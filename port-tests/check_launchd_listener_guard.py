#!/usr/bin/env python3
"""Actual install function and Thumb ABI controls; VM/cache operations modeled."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import re,struct,subprocess,json
repo=Path(__file__).resolve().parents[1];root=repo.parent
out=repo/'out'/('launchd-listener-check-'+datetime.now().strftime('%Y%m%d-%H%M%S-%f'));out.mkdir()
header=(repo/'bsd/dev/arm/i9100_launchd_listener_guard.h').read_text()
body='\n'.join(x for x in header.splitlines() if not x.startswith('#include'))
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t user_addr_t,vm_object_offset_t;
typedef uint32_t vm_map_offset_t,vm_map_size_t,vm_offset_t;
typedef int kern_return_t,boolean_t,vm_prot_t,vm_inherit_t;
typedef void *vm_object_t;
struct map {int dummy;};typedef struct map*vm_map_t;
struct task {int threads;};typedef struct task*task_t;
struct proc {const char*p_comm;unsigned p_pid;task_t task;unsigned char p_uuid[16];unsigned p_idversion;};typedef struct proc*proc_t;
static struct map map;static struct task task={1};static struct proc p;
static unsigned enabled=1,abi=6,irq,interrupts=1,preempt,ops,fail_at,writes,fail_write,reads,checks,active,cleanup_fail;
static unsigned site_cur=5,site_max=5,target_cur=3,target_max=7;
static uint32_t image_words[7]={0xfeedface,12,9,2,12,1000,0};
static unsigned char site_bytes[16],caller_bytes[4],target_bytes[24];
#define FALSE 0
#define TRUE 1
#define KERN_SUCCESS 0
#define VM_MAP_NULL 0
#define VM_OBJECT_NULL 0
#define VM_FLAGS_ANYWHERE 1
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_ALL 7
#define VM_PROT_COPY 16
#define VM_INHERIT_COPY 1
#define OSMemoryBarrier() __sync_synchronize()
static void check(int ok,const char*why){checks++;if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
static int ml_at_interrupt_context(void){return irq;}
static int ml_get_interrupts_enabled(void){return interrupts;}
static int get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char*n,void*out,unsigned size){check(size==4,"boot argument size");*(int*)out=!strcmp(n,"i9100hidabi")?abi:enabled;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned value,volatile uint32_t*at){if(*at!=old)return 0;*at=value;return 1;}
static task_t current_task(void){return &task;}
static vm_map_t current_map(void){return &map;}
static vm_map_t get_task_map(task_t t){check(t==&task,"own map");return &map;}
int get_task_numacts(task_t t){return t->threads;}
static void context(void){check(!irq&&interrupts&&!preempt&&task.threads==1,"no user/VM operation in bad context");}
static int copyin(user_addr_t at,void*buf,unsigned size){context();reads++;
 if(at==0x32000&&size==28)memcpy(buf,image_words,size);
 else if(at==0x3d1a0&&size==16)memcpy(buf,site_bytes,size);
 else if(at==0x34646&&size==4)memcpy(buf,caller_bytes,size);else return 14;return 0;}
static int copyout(const void*buf,user_addr_t at,unsigned size){context();writes++;
 if(at==0x10000&&size==24){check(active&&target_cur==3,"target write permission");if(writes==fail_write)return 14;memcpy(target_bytes,buf,size);}
 else if(at==0x3d1a0&&size==8){check(site_cur==3,"private text write permission");if(writes==fail_write){memcpy(site_bytes,buf,4);return 14;}memcpy(site_bytes,buf,size);}
 else return 14;return 0;}
kern_return_t vm_map_enter(vm_map_t m,vm_map_offset_t*a,vm_map_size_t n,vm_map_offset_t mask,int flags,vm_object_t obj,vm_object_offset_t off,boolean_t copy,vm_prot_t cur,vm_prot_t max,vm_inherit_t inherit){
 context();check(m==&map&&*a==4096&&n==4096&&!mask&&flags==1&&!obj&&!off&&!copy&&cur==3&&max==7&&inherit==1,"real allocation argument ABI");
 if(++ops==fail_at)return 5;*a=0x10000;active=1;return 0;}
static kern_return_t vm_map_protect(vm_map_t m,vm_map_offset_t start,vm_map_offset_t end,vm_prot_t prot,boolean_t maximum){
 context();check(m==&map&&end==start+4096,"bounded protection");if(++ops==fail_at)return 5;
 if(start==0x10000){if(maximum)target_max=prot;else target_cur=prot;}
 else if(start==0x3d000){if(maximum)site_max=prot;else site_cur=prot&7;}else check(0,"unexpected protection target");return 0;}
static kern_return_t vm_map_remove(vm_map_t m,vm_map_offset_t start,vm_map_offset_t end,int flags){context();check(m==&map&&active&&start==0x10000&&end==start+4096&&!flags,"owned allocation cleanup");if(cleanup_fail)return 5;active=0;return 0;}
static void flush_dcache(vm_offset_t at,unsigned size,boolean_t physical){context();(void)at;(void)size;check(!physical,"user virtual cache maintenance");}
static void invalidate_icache(vm_offset_t at,unsigned size,boolean_t physical){flush_dcache(at,size,physical);}
'''
suffix=r'''
static void reset(void){enabled=1;abi=6;irq=preempt=ops=fail_at=writes=fail_write=reads=active=cleanup_fail=0;interrupts=1;task.threads=1;
 site_cur=site_max=5;target_cur=3;target_max=7;image_words[0]=0xfeedface;
 memset((void*)i9100_launchd_listener_diag,0,sizeof(i9100_launchd_listener_diag));i9100_launchd_listener_diag[0]=1;
 p.p_pid=1;p.p_comm="launchd";p.task=&task;p.p_idversion=1;memcpy(p.p_uuid,i9100_launchd_listener_uuid,16);
 memcpy(site_bytes,i9100_launchd_listener_original,16);memcpy(caller_bytes,i9100_launchd_listener_caller,4);}
int main(void){
 reset();i9100_launchd_listener_prepare(&p,0x32000);check(i9100_launchd_listener_diag[1]==2&&i9100_launchd_listener_diag[9]==1&&active,"installed");
 check(site_cur==5&&site_max==5&&target_cur==5&&target_max==5,"maximum RX restored");
 check(((uint32_t*)target_bytes)[5]==0x3d1a9,"resume uses actual image header");unsigned before=reads;i9100_launchd_listener_prepare(&p,0x32000);check(reads==before,"no retry");
 for(unsigned n=1;n<=6;n++){reset();fail_at=n;i9100_launchd_listener_prepare(&p,0x32000);check(i9100_launchd_listener_diag[1]==3&&!active,"VM failure cleanup");check(!memcmp(site_bytes,i9100_launchd_listener_original,16)&&site_cur==5&&site_max==5,"VM failure restores original RX");}
 for(unsigned n=1;n<=2;n++){reset();fail_write=n;i9100_launchd_listener_prepare(&p,0x32000);check(i9100_launchd_listener_diag[1]==3&&!active,"copy failure cleanup");check(!memcmp(site_bytes,i9100_launchd_listener_original,16)&&site_cur==5&&site_max==5,"partial copy restored");}
 reset();fail_at=5;fail_write=3;i9100_launchd_listener_prepare(&p,0x32000);check(i9100_launchd_listener_diag[1]==3&&i9100_launchd_listener_diag[10]==14&&active,"uncertain rollback retains target and records failure");
 reset();fail_at=2;cleanup_fail=1;i9100_launchd_listener_prepare(&p,0x32000);check(i9100_launchd_listener_diag[13]==5&&active,"cleanup failure reported");
 reset();enabled=0;i9100_launchd_listener_prepare(&p,0x32000);check(!reads&&!writes&&!ops,"explicit disabled mode");
 reset();abi=7;i9100_launchd_listener_prepare(&p,0x32000);check(!reads&&!writes&&!ops,"iOS7 excluded");
 reset();p.p_pid=2;i9100_launchd_listener_prepare(&p,0x32000);check(!reads&&!ops,"only PID1");
 reset();p.p_comm="OtherService";i9100_launchd_listener_prepare(&p,0x32000);check(!reads&&!ops,"only launchd");
 for(unsigned n=0;n<4;n++){reset();if(n==0)irq=1;if(n==1)interrupts=0;if(n==2)preempt=1;if(n==3)task.threads=2;i9100_launchd_listener_prepare(&p,0x32000);check(!reads&&!ops&&i9100_launchd_listener_diag[1]==3,"unsafe context rejected before copy/VM");}
 reset();p.task=0;i9100_launchd_listener_prepare(&p,0x32000);check(!reads&&!ops,"missing own task");
 reset();p.p_uuid[0]^=1;i9100_launchd_listener_prepare(&p,0x32000);check(!reads&&!ops,"exact UUID only");
 reset();image_words[0]=0;i9100_launchd_listener_prepare(&p,0x32000);check(!ops,"valid Mach-O required");
 reset();site_bytes[0]^=1;i9100_launchd_listener_prepare(&p,0x32000);check(!ops,"original prologue required");
 reset();caller_bytes[0]^=1;i9100_launchd_listener_prepare(&p,0x32000);check(!ops,"original caller required");
 reset();i9100_launchd_listener_prepare(&p,0x32001);check(!reads&&!ops,"header alignment");
 reset();i9100_launchd_listener_prepare(&p,0x3ffff000);check(!reads&&!ops,"header extent");
 printf("PASS %u production checks: own exec, UUID/header/caller, RX/COW, 6 VM failures, partial writes and rollback uncertainty\n",checks);
}
'''
(out/'check.c').write_text(prefix+body+suffix)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
subprocess.run(['arm-none-eabi-as','-mcpu=cortex-a9','-mthumb',str(repo/'port-tests/launchd-listener-guard-trampoline.s'),'-o',str(out/'guard.o')],check=True)
subprocess.run(['arm-none-eabi-objcopy','-O','binary','-j','.text',str(out/'guard.o'),str(out/'guard.bin')],check=True)
code=(out/'guard.bin').read_bytes()
embedded=bytes(int(n,16) for n in re.findall(r'0x[0-9a-f]+',re.search(r'i9100_launchd_listener_code\[24\]=\{(.*?)\};',header,re.S)[1]))
assert code==embedded
original=bytes(int(n,16) for n in re.findall(r'0x[0-9a-f]+',re.search(r'i9100_launchd_listener_original\[16\]=\{(.*?)\};',header,re.S)[1]))
reference=(root/'research/runtime-launchd-20261115/launchd-rootfs-release').read_bytes()
assert reference[0xc1a0-0x1000:0xc1a0-0x1000+16]==original
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UcError,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_MEM_INVALID
from unicorn.arm_const import *
regs=[globals()['UC_ARM_REG_R'+str(n)] for n in range(13)]
cases=[]
for base in (0x1000,0x32000,0x141000):
 for job in (0,0x11223344):
  for nzcv in (0,0xa0000000,0xf0000000):
   site=base+0xb1a0;target=0x10000000;sp=0x2000f000;end=0x20004000 if not job else site+8
   cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB);cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
   cpu.mem_map(site&~4095,4096);cpu.mem_map(target,4096);cpu.mem_map(0x20000000,0x20000)
   cpu.mem_write(site,struct.pack('<2I',0xf000f8df,target|1)+original[8:])
   cpu.mem_write(target,code[:-4]+struct.pack('<I',(site+8)|1))
   cpu.reg_write(UC_ARM_REG_CPSR,nzcv|0x30)
   initial=[job]+[0x1000+n for n in range(1,13)]
   for r,v in zip(regs,initial):cpu.reg_write(r,v)
   cpu.reg_write(UC_ARM_REG_SP,sp);cpu.reg_write(UC_ARM_REG_LR,0x20004001)
   cpu.emu_start(site|1,end,count=100)
   assert cpu.reg_read(UC_ARM_REG_PC)==end and cpu.reg_read(UC_ARM_REG_CPSR)&0xf0000000==nzcv
   if not job:
    assert cpu.reg_read(UC_ARM_REG_R0)==1105 and cpu.reg_read(UC_ARM_REG_SP)==sp
    assert [cpu.reg_read(r) for r in regs[1:]]==initial[1:]
   else:
    assert cpu.reg_read(UC_ARM_REG_R0)==job and cpu.reg_read(UC_ARM_REG_SP)==sp-32
    assert cpu.reg_read(UC_ARM_REG_R7)==sp-8
    assert struct.unpack('<8I',cpu.mem_read(sp-32,32))==tuple(initial[n] for n in (8,10,11,4,5,6,7))+(0x20004001,)
    assert [cpu.reg_read(r) for n,r in enumerate(regs) if n!=7]==[v for n,v in enumerate(initial) if n!=7]
   cases.append(dict(base=base,null_job=not job,nzcv=nzcv))
# Negative control: the same unpatched original entry faults at j+0x64.
cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB);cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
cpu.mem_map(0x3d000,4096);cpu.mem_map(0x20000000,0x20000);cpu.mem_write(0x3d1a0,original)
cpu.reg_write(UC_ARM_REG_CPSR,0x30)
cpu.reg_write(UC_ARM_REG_R0,0);cpu.reg_write(UC_ARM_REG_SP,0x2000f000);bad=[]
def invalid(uc,access,address,size,value,data):bad.append(address);return False
cpu.hook_add(UC_HOOK_MEM_INVALID,invalid)
try:cpu.emu_start(0x3d1a1,0,count=100)
except UcError:pass
assert bad==[0x64] and cpu.reg_read(UC_ARM_REG_PC)==0x3d1ae
(out/'thumb-controls.json').write_text(json.dumps(dict(cases=cases,original_fault=bad),indent=2))
print('PASS original NULL fault + 18 Thumb controls: return1105, unchanged out parameters/NZCV, exact valid prologue and ASLR resume')
print('EVIDENCE',out)
