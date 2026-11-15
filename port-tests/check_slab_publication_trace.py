#!/usr/bin/env python3
"""Production installer/observer faults and the actual aligned Thumb detour."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                         sys.executable, '-B', __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import hashlib
import json
import struct
import subprocess

repo = Path(__file__).resolve().parents[1]
header = repo/'bsd/dev/arm/i9100_slab_publication_trace.h'
out = repo/'out'/('slab-publication-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
body = '\n'.join(line for line in header.read_text().splitlines() if not line.startswith('#include'))
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32,vm_map_offset_t,vm_map_size_t;
typedef uint64_t user_addr_t,vm_object_offset_t;
typedef void*vm_object_t;
typedef int kern_return_t,boolean_t,vm_prot_t,vm_inherit_t;
typedef unsigned mach_msg_type_number_t;typedef int*task_info_t;
typedef struct {uint64_t all_image_info_addr,all_image_info_size;int all_image_info_format;}task_dyld_info_data_t;
#define TASK_DYLD_INFO 17
#define TASK_DYLD_ALL_IMAGE_INFO_32 0
#define TASK_DYLD_INFO_COUNT (sizeof(task_dyld_info_data_t)/sizeof(unsigned))
struct task {int acts;};typedef struct task*task_t;
struct proc {const char*p_comm;unsigned p_pid;task_t task;uint8_t p_uuid[16];unsigned p_idversion;};typedef struct proc*proc_t;
struct pmap {unsigned token;};typedef struct pmap*pmap_t;
struct map {pmap_t pmap;};typedef struct map*vm_map_t;
static struct pmap pmap;static struct map map={&pmap},other_map={&pmap};
static struct task child={1},parent={1};static task_t current=&child;static struct proc p;
static unsigned enabled=1,abi=6,irq,interrupts=1,preempt,ops,fail_at,fail_again,reads,allocated,removed,copy_fail_addr,dyld_error;
static unsigned wrong_map,change_identity,change_ppn,phys_reads;
static uint8_t main_image[64],shared[0x8000],image[0x8000],codebuf[4096],slab[32],stack[128];
static uint32_t dyld[21];
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
static task_t current_task(void){return current;}
static int task_info(task_t t,unsigned flavor,task_info_t data,unsigned*count){
 assert(t==&child&&flavor==17&&*count==TASK_DYLD_INFO_COUNT);if(dyld_error)return 4;
 task_dyld_info_data_t*info=(void*)data;info->all_image_info_addr=0x25000;info->all_image_info_size=84;info->all_image_info_format=0;return 0;
}
static vm_map_t current_map(void){return wrong_map?&other_map:&map;}
static vm_map_t get_task_map(task_t t){return t==&child?&map:NULL;}
static pmap_t get_task_pmap(task_t t){assert(t==&child);return &pmap;}
static unsigned get_task_numacts(task_t t){return t->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}
static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char*n,int*out,unsigned bytes){assert(bytes==4);*out=!strcmp(n,"i9100slabtrace")?enabled:abi;return 1;}
static int OSCompareAndSwap(unsigned a,unsigned b,volatile unsigned*out){if(*out!=a)return 0;*out=b;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int failing(void){ops++;return ops==fail_at||ops==fail_again;}
static int copyin(user_addr_t a,void*out,unsigned n){
 assert(current==&child&&!irq&&interrupts&&!preempt);reads++;if(a==copy_fail_addr)return 14;
 if(a>=0x32cdc000&&a+n<=0x32ce4000)memcpy(out,image+(a-0x32cdc000),n);
 else if(a==0x65000&&n==28)memcpy(out,main_image,n);
 else if(a==0x25000&&n==84)memcpy(out,dyld,n);
 else if(a>=0x30000&&a+n<=0x30080)memcpy(out,stack+(a-0x30000),n);
 else if(a==0x20000&&n==32){memcpy(out,slab,n);if(change_identity)p.p_idversion++;}
 else return 14;return 0;
}
kern_return_t vm_map_enter(vm_map_t m,vm_map_offset_t*a,vm_map_size_t n,vm_map_offset_t mask,int flags,vm_object_t object,vm_object_offset_t offset,boolean_t copy,vm_prot_t cur,vm_prot_t max,vm_inherit_t inherit){
 assert(m==&map&&n==4096&&!mask&&flags==1&&!object&&!offset&&!copy&&cur==3&&max==7&&inherit==1);
 if(failing())return 4;*a=0x1000;allocated=1;return 0;
}
static int vm_map_protect(vm_map_t m,unsigned a,unsigned end,unsigned prot,int max){assert(m==&map&&end==a+4096&&!max&&(prot==5||prot==19));return failing()?4:0;}
static int copyout(const void*in,user_addr_t a,unsigned n){
 int error=failing();
 if(a==0x1000&&n==64){assert(allocated);memcpy(codebuf,in,error?4:n);}
 else {assert(a==0x32ce2460&&n==8);memcpy(image+(a-0x32cdc000),in,error?4:n);}
 return error?14:0;
}
static int vm_map_remove(vm_map_t m,unsigned a,unsigned end,int flags){assert(m==&map&&a==0x1000&&end==0x2000&&!flags);removed=1;allocated=0;return 0;}
static void flush_dcache(unsigned a,unsigned n,int physical){assert(!physical&&(n==8||n==64));}
static void invalidate_icache(unsigned a,unsigned n,int physical){assert(!physical&&(n==8||n==64));}
static unsigned pmap_find_phys(pmap_t pm,uint64_t a){assert(pm==&pmap&&a==0x20000);phys_reads++;return change_ppn&&phys_reads%2==0?0x40234:0x40123;}
''' + body + r'''
static void reset(void){
 memset((void*)i9100_slab_publication_diag,0,sizeof(i9100_slab_publication_diag));i9100_slab_publication_diag[0]=1;
 memset((void*)i9100_slab_publication_rows,0,sizeof(i9100_slab_publication_rows));
 memset((void*)i9100_slab_failure_mapping_diag,0,sizeof(i9100_slab_failure_mapping_diag));i9100_slab_failure_mapping_diag[0]=1;
 memset(i9100_slab_publication_pending,0,16);i9100_slab_publication_guard=0;
 current=&child;child.acts=1;enabled=interrupts=1;abi=6;
 irq=preempt=ops=fail_at=fail_again=reads=allocated=removed=copy_fail_addr=dyld_error=wrong_map=change_identity=change_ppn=phys_reads=0;
 p.p_comm="backboardd";p.p_pid=8;p.task=&child;p.p_idversion=8;memcpy(p.p_uuid,i9100_slab_publication_uuid,16);
 memset(main_image,0,sizeof(main_image));uint32_t mh[7]={0xfeedface,12,9,2};memcpy(main_image,mh,28);
 memset(shared,0,sizeof(shared));uint32_t ch[7]={0xfeedface,12,9,6,1,24,0},lc[2]={27,24};
 memcpy(shared,ch,28);memcpy(shared+28,lc,8);memcpy(shared+36,i9100_slab_publication_cache_uuid[0],16);
 memcpy(shared+0x6460,i9100_slab_publication_original[0],8);memcpy(image,shared,sizeof(image));
 memset(dyld,0,sizeof(dyld));dyld[0]=12;
 memset(stack,0,sizeof(stack));uint32_t sv[16]={0xb80f0000};sv[5]=0x21000;sv[7]=0x20000;memcpy(stack,sv,64);
 uint32_t expected=0xdeafbe03;memcpy(stack+104,&expected,4);memset(slab,0,sizeof(slab));memcpy(slab+16,&expected,4);
}
static void install(void){i9100_slab_publication_record(&p,0x65000);assert(!i9100_slab_publication_bootstrap(&p,0x30080,0x30));assert(i9100_slab_publication_diag[1]==2);}
static void save(const char*base,const char*suffix,const void*data,unsigned n){char path[1024];snprintf(path,sizeof(path),"%s%s",base,suffix);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(data,1,n,f)==n);fclose(f);}
int main(int argc,char**argv){
 assert(argc==2);reset();current=&parent;i9100_slab_publication_record(&p,0x65000);
 assert(!reads&&!ops&&!i9100_slab_publication_bootstrap(&p,0x30080,0x30));current=&child;
 assert(!i9100_slab_publication_bootstrap(&p,0x30080,0x30)&&i9100_slab_publication_diag[1]==2&&ops==6);
 assert(!memcmp(shared+0x6460,i9100_slab_publication_original[0],8));
 save(argv[1],"",codebuf,64);save(argv[1],".branch",image+0x6460,8);
 unsigned seen=ops;assert(!i9100_slab_publication_bootstrap(&p,0x30080,0x30)&&ops==seen);
 for(unsigned n=1;n<=6;n++){
  reset();i9100_slab_publication_record(&p,0x65000);fail_at=n;
  assert(!i9100_slab_publication_bootstrap(&p,0x30080,0x30));
  assert(i9100_slab_publication_diag[1]==3&&!memcmp(image,shared,sizeof(image)));if(n>1)assert(removed&&!allocated);
 }
 for(unsigned n=7;n<=9;n++){
  reset();i9100_slab_publication_record(&p,0x65000);fail_at=6;fail_again=n;
  assert(i9100_slab_publication_bootstrap(&p,0x30080,0x30)==EIO&&i9100_slab_publication_diag[9]);
  if(n<9)assert(allocated&&!removed);else assert(removed&&!allocated);
 }
 for(unsigned n=0;n<10;n++){
  reset();i9100_slab_publication_record(&p,0x65000);
  if(n==0)enabled=0;if(n==1)abi=7;if(n==2)irq=1;if(n==3)interrupts=0;if(n==4)preempt=1;
  if(n==5)child.acts=2;if(n==6)p.p_idversion++;if(n==7)current=&parent;if(n==8)wrong_map=1;if(n==9)p.p_comm="SpringBoard";
  assert(!i9100_slab_publication_bootstrap(&p,0x30080,0x30)&&!ops&&!reads);
 }
 reset();p.p_uuid[0]^=1;i9100_slab_publication_record(&p,0x65000);assert(!i9100_slab_publication_pending[0]);
 for(unsigned n=0;n<5;n++){
  reset();i9100_slab_publication_record(&p,0x65000);
  if(n==0)image[0x6460]^=1;if(n==1)image[36]^=1;if(n==2)dyld[20]=1;if(n==3)dyld_error=1;if(n==4)copy_fail_addr=0x65000;
  assert(!i9100_slab_publication_bootstrap(&p,0x30080,0x30)&&!ops&&i9100_slab_publication_diag[1]==3);
 }
 for(unsigned n=0;n<4;n++){
  reset();dyld[20]=(unsigned[]){0x02e9f000,0x0375b000,0x0375c000,0x02e9f001}[n];
  i9100_slab_publication_record(&p,0x65000);assert(!i9100_slab_publication_bootstrap(&p,0x30080,0x30)&&!ops);
  assert(i9100_slab_publication_diag[8]==(n<2?7:6));
 }
 reset();install();i9100_slab_publication_note(&p,0x1016,0x30000);
 volatile uint32_t*r=&i9100_slab_publication_rows[4];
 assert(r[0]==1&&r[4]==0x20000&&r[5]==0x21000&&r[6]==0xdeafbe03&&r[7]==r[6]&&!r[8]&&!r[9]&&!r[10]);
 assert(r[11]==0x40123&&r[12]==r[11]&&r[20]==r[6]);
 save(argv[1],".diag",(const void*)i9100_slab_publication_diag,sizeof(i9100_slab_publication_diag));
 save(argv[1],".rows",(const void*)i9100_slab_publication_rows,sizeof(i9100_slab_publication_rows));
 unsigned count=i9100_slab_publication_rows[0];i9100_slab_publication_note(&p,0x1018,0x30000);assert(i9100_slab_publication_rows[0]==count);
 irq=1;reads=0;i9100_slab_publication_note(&p,0x1016,0x30000);assert(!reads);irq=0;
 change_identity=1;i9100_slab_publication_note(&p,0x1016,0x30000);assert(i9100_slab_publication_rows[0]==count);
 reset();install();copy_fail_addr=0x30000;i9100_slab_publication_note(&p,0x1016,0x30000);r=&i9100_slab_publication_rows[4];assert(r[8]==14&&r[9]==UINT32_MAX&&r[10]==14);
 reset();install();copy_fail_addr=0x30068;i9100_slab_publication_note(&p,0x1016,0x30000);assert(i9100_slab_publication_rows[4+10]==14);
 reset();install();copy_fail_addr=0x20000;i9100_slab_publication_note(&p,0x1016,0x30000);assert(i9100_slab_publication_rows[4+9]==14);
 reset();install();for(unsigned n=0;n<65;n++)i9100_slab_publication_note(&p,0x1016,0x30000);
 assert(i9100_slab_publication_rows[0]==64&&i9100_slab_publication_diag[16]==65&&i9100_slab_publication_diag[17]==1);
 reset();install();change_ppn=1;memset(slab,0,sizeof(slab));i9100_slab_publication_failure(&p,0x20000);
 r=i9100_slab_failure_mapping_diag;assert(r[0]==1&&r[1]==1&&r[5]==0x20000&&r[8]==0x40123&&r[9]==0x40234&&!r[10]&&!r[11]);
 save(argv[1],".failure",(const void*)r,64);reads=0;i9100_slab_publication_failure(&p,0x20000);assert(!reads);
 reset();install();p.p_idversion++;reads=0;i9100_slab_publication_failure(&p,0x20000);assert(!reads&&!i9100_slab_failure_mapping_diag[1]);
 puts("PASS production: private COW, partial-write rollback, six installation failures, three unsafe rollback stops, scope, UUID/bytes, stack/header faults, identity recheck, capacity and failure PPN snapshots");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang', '-O1', '-g', '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', out/'check.c', '-o', out/'check'], check=True)
subprocess.run([out/'check', out/'code.bin'], check=True)
sys.path.insert(0, str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import *
regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4,
        UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9,
        UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR]
site = 0x32ce2460
original = bytes.fromhex('18681d602e6005606860')
code = (out/'code.bin').read_bytes()
branch = (out/'code.bin.branch').read_bytes()
assert site & 3 == 0 and struct.unpack('<2I', branch) == (0xf000f8df, 0x1001)
results = []
for traced in (False, True):
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    for address, size in ((site&~4095,4096), (0x1000,4096), (0x20000,0x3000), (0x20000000,0x20000)):
        u.mem_map(address, size)
    u.mem_write(site, original)
    if traced:
        u.mem_write(site, branch)
        u.mem_write(0x1000, code)
    u.mem_write(0x21004, struct.pack('<I',0x22000))
    u.mem_write(0x20010, struct.pack('<I',0xdeafbe03))
    u.reg_write(UC_ARM_REG_CPSR, 0xb80f0030)
    for n, register in enumerate(regs[:13]):
        u.reg_write(register, 0x7000+n)
    for register, value in ((UC_ARM_REG_R3,0x21004),(UC_ARM_REG_R5,0x20000),
                            (UC_ARM_REG_R6,0x21000),(UC_ARM_REG_SP,0x20010000),(UC_ARM_REG_LR,0x2001ff01)):
        u.reg_write(register,value)
    u.mem_write(0x20010028,struct.pack('<I',0xdeafbe03))
    calls=[]
    def hook(uc, at, size, arg):
        if at == site+10:
            uc.emu_stop()
        if traced and at == 0x1014:
            assert uc.reg_read(UC_ARM_REG_R0)==1 and uc.reg_read(UC_ARM_REG_R12)==20
            sp=uc.reg_read(UC_ARM_REG_SP)
            saved=struct.unpack('<16I',uc.mem_read(sp,64))
            assert saved[7]==0x20000 and saved[5]==0x21004
            assert struct.unpack('<I',uc.mem_read(sp+104,4))[0]==0xdeafbe03
            calls.append(at)
            uc.reg_write(UC_ARM_REG_R0,8)
            uc.reg_write(UC_ARM_REG_R1,1)
            uc.reg_write(UC_ARM_REG_CPSR,uc.reg_read(UC_ARM_REG_CPSR)^(1<<29))
            uc.reg_write(UC_ARM_REG_PC,(at+2)|1)
    u.hook_add(UC_HOOK_CODE,hook)
    u.emu_start(site|1,site+12,count=1000)
    assert len(calls)==int(traced)
    results.append(([u.reg_read(r) for r in regs],bytes(u.mem_read(0x20000,0x3000)),bytes(u.mem_read(0x20010000,128))))
assert results[0]==results[1], 'Detour changed original registers, flags, caller stack or linked-list writes'
report=dict(result='PASS',header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),
            production_fault_tests=True,aligned_site=hex(site),replayed_bytes=original[:8].hex(),
            final_original_store=hex(site+8),loop_target_intact=hex(site+10),actual_production_thumb_branch=True,
            cortex_a9_registers_flags_caller_stack_and_four_stores_identical=True)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS actual production Cortex-A9 detour; original list stores, registers, flags and caller stack match')
print('EVIDENCE',out)
