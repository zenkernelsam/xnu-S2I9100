#!/usr/bin/env python3
"""Check production GS density COW rollback and real Cortex-A9 register state."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess,struct,json,ast
repo=Path(__file__).resolve().parents[1]
out=repo/'out'/('ui-density-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
body='\n'.join(line for line in (repo/'bsd/dev/arm/i9100_ui_density.h').read_text().splitlines() if not line.startswith('#include'))
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UInt32,vm_map_offset_t,vm_map_size_t;typedef uint64_t user_addr_t,vm_object_offset_t;
typedef void *vm_object_t;typedef int vm_prot_t,vm_inherit_t,kern_return_t;typedef unsigned mach_msg_type_number_t;typedef int *task_info_t;
typedef struct {uint64_t all_image_info_addr,all_image_info_size;int all_image_info_format;}task_dyld_info_data_t;
struct task {unsigned acts;};typedef struct task *task_t;
struct proc {const char *p_comm;unsigned p_pid;task_t task;uint8_t p_uuid[16];unsigned p_idversion;};typedef struct proc *proc_t;
typedef int *vm_map_t;
#define TASK_DYLD_INFO 17
#define TASK_DYLD_INFO_COUNT (sizeof(task_dyld_info_data_t)/sizeof(unsigned))
#define TASK_DYLD_ALL_IMAGE_INFO_32 0
#define KERN_SUCCESS 0
#define FALSE 0
#define VM_OBJECT_NULL NULL
#define VM_FLAGS_ANYWHERE 1
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_COPY 16
#define VM_PROT_ALL 7
#define VM_INHERIT_COPY 1
#define EIO 5
static struct task child={1},other={1};static task_t current=&child;static int map;
static struct proc p;static unsigned option,abi,native,irq,interrupts,preempt,ops,fail_at,fail_again,allocated,removed,slide,read_fail,different_map;
static uint8_t image[64],original[2][8],private_bytes[2][8],code[4096];static uint32_t cache[2],dyld[21],saved[16];static int writable;
static int other_map;
static task_t current_task(void){return current;}
static vm_map_t current_map(void){return different_map?&other_map:&map;}
static vm_map_t get_task_map(task_t task){return task==&child?&map:NULL;}
static unsigned get_task_numacts(task_t task){return task->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}
static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char *key,int *value,unsigned n){assert(n==4);*value=!strcmp(key,"i9100uidensity")?option:!strcmp(key,"i9100hidabi")?abi:native;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned next,volatile unsigned *value){if(*value!=old)return 0;*value=next;return 1;}
static unsigned publication_probe,publication_step;
static void publication_barrier(void);
#define OSMemoryBarrier() do { __sync_synchronize();publication_barrier(); } while(0)
static int task_info(task_t task,unsigned flavor,task_info_t data,unsigned *count){assert(task==&child&&flavor==17&&*count==TASK_DYLD_INFO_COUNT);task_dyld_info_data_t *info=(void*)data;info->all_image_info_addr=0x25000;info->all_image_info_size=84;info->all_image_info_format=0;return 0;}
static int copyin(user_addr_t address,void *destination,unsigned n){
 assert(current==&child&&!irq&&interrupts&&!preempt);if(address==read_fail)return 14;
 if(address==0x25000&&n==84)memcpy(destination,dyld,n);
 else if(address>=0x34c2f000U+slide&&address+n<=0x34c2f000U+slide+sizeof(image))memcpy(destination,image+(address-(0x34c2f000U+slide)),n);
 else if(address==0x3a283e7cU+slide&&n==4)memcpy(destination,cache,n);
 else if(address==0x34c34d44U+slide&&n==8)memcpy(destination,private_bytes[0],n);
 else if(address==0x34c34c84U+slide&&n==8)memcpy(destination,private_bytes[1],n);
 else if(address==0x27000&&n==64)memcpy(destination,saved,n);
 else if(address==0x26000&&n==8){uint64_t base=0x30000000U+slide;memcpy(destination,&base,8);}
 else return 14;return 0;
}
static int failing(void){ops++;return ops==fail_at||ops==fail_again;}
static kern_return_t vm_map_enter(vm_map_t m,vm_map_offset_t *address,vm_map_size_t n,vm_map_offset_t mask,int flags,vm_object_t object,vm_object_offset_t offset,int needcopy,vm_prot_t protection,vm_prot_t maximum,vm_inherit_t inherit){
 assert(m==&map&&n==4096&&!mask&&flags==1&&!object&&!offset&&!needcopy&&protection==3&&maximum==7&&inherit==1);
 if(failing())return 4;*address=0x1000;allocated++;return 0;
}
static kern_return_t vm_map_protect(vm_map_t m,unsigned start,unsigned end,unsigned flags,int maximum){
 assert(m==&map&&end==start+4096&&!maximum);
 if(failing())return 4;
 if(start==0x1000)assert(flags==5);
 else {assert(start==((0x34c34d44U+slide)&~4095U));assert(flags==5||flags==(3|16));writable=flags==(3|16);}
 return 0;
}
static int copyout(const void *source,user_addr_t address,unsigned n){
 int failed=failing();
 if(address==0x1000&&n==192)memcpy(code,source,failed?n/2:n);
 else {assert((address==0x34c34d44U+slide||address==0x34c34c84U+slide)&&n==8&&writable);memcpy(private_bytes[address==0x34c34c84U+slide],source,failed?n/2:n);}
 return failed?14:0;
}
static kern_return_t vm_map_remove(vm_map_t m,unsigned start,unsigned end,unsigned flags){assert(m==&map&&start==0x1000&&end==0x2000&&!flags);removed++;return 0;}
static void flush_dcache(unsigned address,unsigned n,int physical){assert(!physical&&(n==8||n==192));}
static void invalidate_icache(unsigned address,unsigned n,int physical){assert(!physical&&(n==8||n==192));}
'''
suffix=r'''
static void publication_barrier(void){
 if(!publication_probe)return;
 volatile uint32_t *a=i9100_ui_density_admission[0];
 if(publication_step<2)assert((a[1]&1U) && a[1]==a[19]);
 if(publication_step==2)assert((a[1]&1U) && a[19]==a[1]+1U);
 if(publication_step==3)assert(!(a[1]&1U) && a[1]==a[19]);
 publication_step++;
}
static void reset(unsigned slot){
 memset((void*)i9100_ui_density_diag,0,sizeof(i9100_ui_density_diag));memset(i9100_ui_density_pending,0,sizeof(i9100_ui_density_pending));memset((void*)i9100_ui_density_guard,0,sizeof(i9100_ui_density_guard));
 memset((void*)i9100_ui_density_execution,0,sizeof(i9100_ui_density_execution));memset(saved,0,sizeof(saved));saved[7]=0x3a283e50;saved[15]=0x30003451;
 memset((void*)i9100_ui_density_admission,0,sizeof(i9100_ui_density_admission));different_map=publication_probe=publication_step=0;
 memset(image,0,sizeof(image));memset(code,0,sizeof(code));memset(cache,0,sizeof(cache));memset(dyld,0,sizeof(dyld));
 uint32_t mh[7]={0xfeedface,12,0,6,1,24,0},lc[2]={27,24};memcpy(image,mh,28);memcpy(image+28,lc,8);memcpy(image+36,i9100_ui_density_gs_uuid,16);
 memcpy(original[0],i9100_ui_density_original,8);memcpy(original[1],i9100_ui_density_init_original,8);memcpy(private_bytes,original,16);
 option=1333;abi=6;native=1;irq=preempt=ops=fail_at=fail_again=allocated=removed=slide=read_fail=0;interrupts=1;current=&child;child.acts=1;writable=0;dyld[0]=12;
 p.p_comm=i9100_ui_density_names[slot];p.p_pid=slot+8;p.task=&child;p.p_idversion=slot+8;memcpy(p.p_uuid,i9100_ui_density_uuids[slot],16);
}
static int boot(void){return i9100_ui_density_bootstrap(&p,0x30001001,0x10);}
int main(int argc,char **argv){
 assert(argc==2);
 for(unsigned slot=0;slot<3;slot++){
  reset(slot);i9100_ui_density_record(&p,0x65000);assert(!boot()&&i9100_ui_density_diag[slot][1]==2&&i9100_ui_density_diag[slot][10]==1&&allocated==1&&!removed&&!writable);
  assert(!memcmp(original[0],i9100_ui_density_original,8));assert(memcmp(private_bytes,original,16));unsigned before=ops;assert(!boot()&&ops==before);
 }
 /* Admission observes failed initial gates without relaxing them or copying
  * a rejected user stack. The actual execution counter remains authoritative. */
 for(unsigned test=0;test<12;test++){
  reset(0);i9100_ui_density_record(&p,0x65000);assert(!boot());
  unsigned pc=0x1016,sp=0x27000,phase=1,expected=0;
  if(test==1){p.p_uuid[0]^=1;expected=1;}
  if(test==2){current=&other;expected=2;}
  if(test==3){different_map=1;expected=4;}
  if(test==4){irq=1;expected=8;}
  if(test==5){interrupts=0;expected=16;}
  if(test==6){preempt=1;expected=32;}
  if(test==7){i9100_ui_density_diag[0][1]=1;expected=64;}
  if(test==8){p.p_idversion++;expected=256;}
  if(test==9){pc+=2;expected=512;}
  if(test==10){sp=3;expected=1024;}
  if(test==11){phase=4;expected=512;}
  publication_probe=1;publication_step=0;i9100_ui_density_note(&p,pc,sp,phase);publication_probe=0;
  assert(publication_step>=4);
  assert(i9100_ui_density_admission[0][4]==1 && i9100_ui_density_admission[0][5]==1);
  assert(i9100_ui_density_admission[0][6]==expected && i9100_ui_density_admission[0][1]==i9100_ui_density_admission[0][19]);
  assert(i9100_ui_density_execution[0][3]==(test==0?1U:0U));
 }
 reset(0);i9100_ui_density_record(&p,0x65000);assert(!boot());
 i9100_ui_density_note(&p,0x30000100,0x27000,77);
 assert(i9100_ui_density_admission[0][4]==1 && !i9100_ui_density_admission[0][5]);
 i9100_ui_density_guard[0]=1;i9100_ui_density_note(&p,0x1016,0x27000,1);
 assert(i9100_ui_density_admission[0][18]==2 && !i9100_ui_density_execution[0][3]);
 i9100_ui_density_guard[0]=0;i9100_ui_density_note(&p,0x1016,0x27000,1);
 p.p_idversion++;i9100_ui_density_note(&p,0x30000100,0x27000,77);
 assert(i9100_ui_density_admission[0][4]==1 && !i9100_ui_density_admission[0][5] && i9100_ui_density_admission[0][18]==2);
 for(unsigned n=1;n<=9;n++){
  reset(0);i9100_ui_density_record(&p,0x65000);fail_at=n;assert(!boot()&&i9100_ui_density_diag[0][1]==3&&!memcmp(private_bytes,original,16)&&!writable);
  assert(!allocated||removed==1);
 }
 reset(0);i9100_ui_density_record(&p,0x65000);fail_at=5;fail_again=7;assert(boot()==EIO&&i9100_ui_density_diag[0][9]&&!removed);
 for(unsigned test=0;test<10;test++){
  reset(0);
  if(test==0)option=0;if(test==1)option=2000;if(test==2)abi=7;if(test==3)native=0;if(test==4)p.p_uuid[0]^=1;if(test==5)p.p_comm="Other";
  i9100_ui_density_record(&p,0x65000);
  if(test==6)current=&other;if(test==7)child.acts=2;if(test==8)irq=1;if(test==9)preempt=1;
  assert(!boot()&&!ops);
 }
 for(unsigned test=0;test<7;test++){
  reset(0);i9100_ui_density_record(&p,0x65000);
  if(test==0)image[36]^=1;if(test==1)cache[0]=0x3f800000;if(test==2)cache[0]=0x3fc00000;
  if(test==3)private_bytes[0][0]^=1;if(test==4)read_fail=0x3a283e7c;if(test==5)dyld[20]=0x0375c000;if(test==6)dyld[20]=1;
  assert(!boot()&&!ops&&i9100_ui_density_diag[0][1]==3);
 }
 for(unsigned value=1333;value<=1500;value+=167)for(unsigned relocation=0;relocation<3;relocation++){
  reset(0);option=value;slide=relocation==0?0:relocation==1?0x20c000:0x0375b000;dyld[20]=slide;
  i9100_ui_density_record(&p,0x65000);assert(!boot()&&i9100_ui_density_diag[0][1]==2);
  char file[512];snprintf(file,sizeof(file),"%s/%u-%08x.bin",argv[1],value,slide);FILE *f=fopen(file,"wb");assert(f);assert(fwrite(code,1,192,f)==192);fclose(f);
 }
 reset(0);dyld[0]=0;i9100_ui_density_record(&p,0x65000);assert(!i9100_ui_density_region_completed(&p,0x30001001,0x10,294,0x26000)&&i9100_ui_density_diag[0][1]==2&&i9100_ui_density_diag[0][19]==294);
 reset(0);dyld[0]=0;i9100_ui_density_record(&p,0x65000);assert(!i9100_ui_density_region_completed(&p,0x30001001,0x10,438,0)&&i9100_ui_density_diag[0][1]==2);
 reset(1);i9100_ui_density_guard[0]=1;i9100_ui_density_record(&p,0x65000);assert(!boot()&&i9100_ui_density_diag[1][1]==2);
 reset(0);option=0;assert(!i9100_ui_density_region_completed(&p,0x30001001,0x10,294,0)&&!ops);
 reset(0);i9100_ui_density_record(&p,0x65000);assert(!boot());cache[0]=0x3f800000;i9100_ui_density_note(&p,0x1016,0x27000,1);assert(i9100_ui_density_execution[0][3]==1&&i9100_ui_density_execution[0][10]==0x3f800000);
 cache[0]=0x3faaaaab;i9100_ui_density_note(&p,0x1056,0x27000,2);assert(i9100_ui_density_execution[0][4]==1&&i9100_ui_density_execution[0][10]==0x3faaaaab&&!i9100_ui_density_execution[0][6]);
 i9100_ui_density_note(&p,0x1096,0x27000,3);assert(i9100_ui_density_execution[0][5]==1);
 char admission_file[512];snprintf(admission_file,sizeof(admission_file),"%s/admission.bin",argv[1]);
 FILE *admission=fopen(admission_file,"wb");assert(admission);
 assert(fwrite((const void*)i9100_ui_density_admission,1,sizeof(i9100_ui_density_admission),admission)==sizeof(i9100_ui_density_admission));fclose(admission);
 puts("PASS production scope, published-GS refusal, nine partial-write rollbacks, unsafe-restore stop, early mapping and actual execution observations");
}
'''
(out/'check.c').write_text(prefix+body+suffix)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',out/'check.c','-o',out/'check'],check=True)
subprocess.run([out/'check',out],check=True)
decoder_file=repo.parent/'uniloader-S2I9100/scripts/i9100/inspect-ui-density.py'
tree=ast.parse(decoder_file.read_text())
nodes=[node for node in tree.body if (isinstance(node,ast.Assign) and any(
    isinstance(target,ast.Name) and target.id in ('ADMISSION_FIELDS','REJECTIONS') for target in node.targets)) or
    (isinstance(node,ast.FunctionDef) and node.name=='decode_admission')]
namespace={'struct':struct};exec(compile(ast.Module(body=nodes,type_ignores=[]),str(decoder_file),'exec'),namespace)
admission_raw=(out/'admission.bin').read_bytes()
decoded=namespace['decode_admission'](admission_raw)
assert decoded['backboardd']['consistent'] and decoded['backboardd']['private_entries']==3
assert not decoded['backboardd']['initial_gate_rejections'] and not decoded['SpringBoard']['consistent']
for head,tail in ((7,6),(6,7),(6,8),(7,8)):
    partial=bytearray(admission_raw);struct.pack_into('<I',partial,4,head);struct.pack_into('<I',partial,19*4,tail)
    assert not namespace['decode_admission'](bytes(partial))['backboardd']['consistent']
unknown=bytearray(admission_raw);struct.pack_into('<I',unknown,6*4,2048)
try:namespace['decode_admission'](bytes(unknown))
except ValueError:pass
else:raise AssertionError('Unknown admission rejection accepted')
(out/'admission-decoded.json').write_text(json.dumps(decoded,indent=2)+'\n')
print('PASS production admission reader; odd/mismatched publication is not interpreted as a complete context')
sys.path.insert(0,str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *
registers=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_CPSR]
cases=0
for value in (1333,1500):
    for slide in (0,0x20c000,0x0375b000):
        site=0x34c34d44+slide;resume=site+8;state=0x3a283e50+slide
        for flags in (0,0x80000000,0x30000000,0xf0000000):
            states=[]
            for calibrated in (False,True):
                u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
                u.mem_map(0x1000,4096);u.mem_map(site&~4095,4096);u.mem_map(0x200000,4096);u.mem_map(state&~4095,4096)
                u.mem_write(0x1000,(out/f'{value}-{slide:08x}.bin').read_bytes())
                u.mem_write(site,struct.pack('<2I',0xf000f8df,0x1001) if calibrated else bytes.fromhex('87ef101f95ed0b0a'))
                u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
                u.reg_write(UC_ARM_REG_CPSR,flags|0x30)
                for index,reg in enumerate(registers[:-1]):u.reg_write(reg,0x110000+index*256)
                u.reg_write(UC_ARM_REG_SP,0x200800);u.reg_write(UC_ARM_REG_R2,0x200820);u.reg_write(UC_ARM_REG_R11,1)
                u.reg_write(UC_ARM_REG_R5,state);u.mem_write(state+44,struct.pack('<I',0x3f800000))
                for n in range(32):u.reg_write(UC_ARM_REG_S0+n,0x3f000000+n)
                observed=[]
                def trace(machine,at,size,user):
                    if at==resume:machine.emu_stop()
                    if at in (0x1014,0x1054,0x1094):
                        observed.append(struct.unpack('<I',machine.mem_read(state+44,4))[0])
                        machine.reg_write(UC_ARM_REG_R0,999);machine.reg_write(UC_ARM_REG_R12,0xdeadbeef)
                        machine.reg_write(UC_ARM_REG_CPSR,(machine.reg_read(UC_ARM_REG_CPSR)&0x0fffffff)|0x90000000)
                        machine.reg_write(UC_ARM_REG_PC,(at+2)|1)
                u.hook_add(UC_HOOK_CODE,trace)
                u.emu_start(site|1,resume+2,count=128)
                assert u.reg_read(UC_ARM_REG_PC)==resume
                states.append(([u.reg_read(r) for r in registers],struct.unpack('<I',u.mem_read(state+44,4))[0],[u.reg_read(UC_ARM_REG_S0+n) for n in range(32)],u.reg_read(UC_ARM_REG_FPSCR)))
                if calibrated:assert observed==[0x3f800000,(0x3faaaaab if value==1333 else 0x3fc00000)]
            original,new=states;assert original[0]==new[0]
            assert original[1]==0x3f800000 and new[1]==(0x3faaaaab if value==1333 else 0x3fc00000)
            assert original[2][1:]==new[2][1:] and original[3]==new[3]
            assert original[2][0]==original[1] and new[2][0]==new[1]
            cases+=1
(out/'result.json').write_text(json.dumps(dict(result='PASS',native_abi_cases=cases,scope='original BB/SB/Setup only; real UI and all downstream metrics remain hardware checks'),indent=2)+'\n')
print('PASS Cortex-A9 GS calibration, all GPR/APSR/FPSCR and other VFP registers; '+str(cases)+' cases; '+str(out))
