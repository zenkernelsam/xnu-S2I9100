#!/usr/bin/env python3
"""Production Setup observer transaction/scope and actual Thumb prologues."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess,struct,json
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('ui-execution-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
body='\n'.join(x for x in (repo/'bsd/dev/arm/i9100_ui_execution_trace.h').read_text().splitlines() if not x.startswith('#include'))
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UInt32,vm_map_offset_t,vm_map_size_t;typedef void*vm_object_t;typedef uint64_t vm_object_offset_t,user_addr_t;
typedef int vm_prot_t,vm_inherit_t,kern_return_t,boolean_t;typedef unsigned mach_msg_type_number_t;typedef int*task_info_t;
typedef struct {uint64_t all_image_info_addr,all_image_info_size;int all_image_info_format;}task_dyld_info_data_t;
struct task {unsigned acts;};typedef struct task*task_t;
struct proc {const char*p_comm;unsigned p_pid;task_t task;uint8_t p_uuid[16];unsigned p_idversion;};typedef struct proc*proc_t;
struct map {int x;};typedef struct map*vm_map_t;
static struct task child={1},parent={1};static task_t current=&child;static struct map map;static struct proc p;
static unsigned enabled=1,abi=6,retina,irq,interrupts=1,preempt,ops,fail_at,fail_again,reads,allocated,removed,read_fail,dyld_error;
static uint8_t image[0x160000],shared[0x160000],exe[0x20000],exeshared[0x20000],mh[28],codebuf[4096],actor[784],event[128];static uint32_t dyld[21],saved[16];
#define TASK_DYLD_INFO 17
#define TASK_DYLD_INFO_COUNT (sizeof(task_dyld_info_data_t)/sizeof(unsigned))
#define TASK_DYLD_ALL_IMAGE_INFO_32 0
#define TRUE 1
#define FALSE 0
#define KERN_SUCCESS 0
#define VM_OBJECT_NULL NULL
#define VM_FLAGS_ANYWHERE 1
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_COPY 16
#define VM_PROT_ALL 7
#define VM_INHERIT_COPY 1
#define EIO 5
static task_t current_task(void){return current;}
static vm_map_t current_map(void){return &map;}
static vm_map_t get_task_map(task_t t){return t==&child?&map:NULL;}
static unsigned get_task_numacts(task_t t){return t->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}
static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char*n,int*out,unsigned s){assert(s==4);*out=!strcmp(n,"i9100uitrace")?enabled:!strcmp(n,"i9100uiretina")?retina:abi;return 1;}
static int OSCompareAndSwap(unsigned a,unsigned b,volatile unsigned*p){if(*p!=a)return 0;*p=b;return 1;}
#define OSMemoryBarrier() __sync_synchronize()
static int task_info(task_t t,unsigned flavor,task_info_t data,unsigned*count){assert(t==&child&&flavor==17&&*count==TASK_DYLD_INFO_COUNT);if(dyld_error)return 4;task_dyld_info_data_t*i=(void*)data;i->all_image_info_addr=0x25000;i->all_image_info_size=84;i->all_image_info_format=0;return 0;}
static int copyin(user_addr_t a,void*out,unsigned n){
 assert(current==&child&&!irq&&interrupts&&!preempt);reads++;if(a==read_fail)return 14;
 if(a==0x65000&&n==28)memcpy(out,mh,n);
 else if(a==0x3310e9c4&&n==8){static uint8_t bytes[8]={0x10,0x46,0x79,0xf6,0x87,0xbf,0,0xbf};memcpy(out,bytes,8);}
 else if(a==0x333ae22c&&n==29)memcpy(out,"setPreferredSharedImageScale:",29);
 else if(a>=0x32f2f000&&a+n<=0x3308f000)memcpy(out,image+(a-0x32f2f000),n);
 else if(a>=0x65000&&a+n<=0x85000)memcpy(out,exe+(a-0x65000),n);
 else if(a==0x25000&&n==84)memcpy(out,dyld,n);
 else if(a==0x30000&&n==64)memcpy(out,saved,n);
 else if(a>=0x20000&&a+n<=0x20000+sizeof(actor))memcpy(out,actor+(a-0x20000),n);
 else if(a==0x40000&&n<=128)memcpy(out,event,n);
 else return 14;return 0;
}
static int failing(void){ops++;return ops==fail_at||ops==fail_again;}
kern_return_t vm_map_enter(vm_map_t m,vm_map_offset_t*a,vm_map_size_t n,vm_map_offset_t mask,int flags,vm_object_t obj,vm_object_offset_t off,boolean_t copy,vm_prot_t cur,vm_prot_t max,vm_inherit_t inherit){assert(m==&map&&n==4096&&!mask&&flags==1&&!obj&&!off&&!copy&&cur==3&&max==7&&inherit==1);if(failing())return 4;*a=0x1000;allocated=1;return 0;}
static int vm_map_protect(vm_map_t m,unsigned a,unsigned e,unsigned prot,int max){assert(m==&map&&e==a+4096&&!max&&(prot==5||prot==19));return failing()?4:0;}
static int copyout(const void*in,user_addr_t a,unsigned n){int error=failing();if(a==0x1000&&n==656)memcpy(codebuf,in,error?4:n);else if(a>=0x65000&&a+n<=0x85000){assert(n==8);memcpy(exe+(a-0x65000),in,error?4:n);}else{assert(a>=0x32f2f000&&a+n<=0x3308f000&&n==8);memcpy(image+(a-0x32f2f000),in,error?4:n);}return error?14:0;}
static int vm_map_remove(vm_map_t m,unsigned a,unsigned e,int f){assert(m==&map&&a==0x1000&&e==0x2000&&!f);allocated=0;removed=1;return 0;}
static void flush_dcache(unsigned a,unsigned n,int physical){assert(!physical&&(n==8||n==656));}
static void invalidate_icache(unsigned a,unsigned n,int physical){assert(!physical&&(n==8||n==656));}
''' + body + r'''
static int bootstrap(void){return i9100_ui_execution_bootstrap(&p,0x30080,0x30);}
static void reset(void){
 memset((void*)i9100_ui_execution_diag,0,sizeof(i9100_ui_execution_diag));i9100_ui_execution_diag[0]=2;
 memset((void*)i9100_ui_execution_rows,0,sizeof(i9100_ui_execution_rows));memset(i9100_ui_execution_pending,0,16);i9100_ui_execution_guard=0;
 current=&child;child.acts=1;enabled=interrupts=1;abi=6;retina=irq=preempt=ops=fail_at=fail_again=reads=allocated=removed=read_fail=dyld_error=0;
 p.p_comm="Setup";p.p_pid=104;p.task=&child;p.p_idversion=104;memcpy(p.p_uuid,i9100_ui_execution_uuid,16);
 uint32_t h[7]={0xfeedface,12,9,2};memcpy(mh,h,28);memset(dyld,0,84);dyld[0]=12;
 memset(shared,0,sizeof(shared));h[3]=6;h[4]=1;h[5]=24;memcpy(shared,h,28);uint32_t lc[2]={27,24};memcpy(shared+28,lc,8);memcpy(shared+36,i9100_ui_execution_cache_uuid,16);
 memset(exeshared,0,sizeof(exeshared));
 for(unsigned n=0;n<I9100_UI_SITES;n++)memcpy(n==8?exeshared+i9100_ui_execution_sites[n]:shared+(i9100_ui_execution_sites[n]-0x32f2f000),i9100_ui_execution_original[n],8);
 memcpy(image,shared,sizeof(image));memcpy(exe,exeshared,sizeof(exe));memset(saved,0,64);saved[2]=0x20000;saved[4]=saved[5]=0x40000;memset(actor,0xa5,sizeof(actor));memset(event,0x5a,128);
}
static void install(void){i9100_ui_execution_record(&p,0x65000);assert(!bootstrap()&&i9100_ui_execution_diag[1]==2);}
int main(int argc,char**argv){
 assert(argc==2);reset();current=&parent;i9100_ui_execution_record(&p,0x65000);assert(!bootstrap()&&!reads&&!ops);current=&child;assert(!bootstrap()&&i9100_ui_execution_diag[1]==2);
 FILE*f=fopen(argv[1],"wb");assert(f);fwrite(codebuf,1,656,f);fclose(f);assert(!memcmp(shared+(i9100_ui_execution_sites[0]-0x32f2f000),i9100_ui_execution_original[0],8));
 for(unsigned n=1;n<=30;n++){reset();i9100_ui_execution_record(&p,0x65000);fail_at=n;assert(!bootstrap()&&i9100_ui_execution_diag[1]==3&&!memcmp(image,shared,sizeof(image))&&!memcmp(exe,exeshared,sizeof(exe)));if(n>1)assert(removed&&!allocated);}
 reset();i9100_ui_execution_record(&p,0x65000);fail_at=6;fail_again=7;assert(bootstrap()==EIO&&i9100_ui_execution_diag[9]&&allocated&&!removed);
 for(unsigned n=0;n<8;n++){reset();i9100_ui_execution_record(&p,0x65000);if(n==0)enabled=0;if(n==1)abi=7;if(n==2)irq=1;if(n==3)interrupts=0;if(n==4)preempt=1;if(n==5)child.acts=2;if(n==6)p.p_idversion++;if(n==7)current=&parent;assert(!bootstrap()&&!reads&&!ops);}
 reset();image[36]^=1;i9100_ui_execution_record(&p,0x65000);assert(!bootstrap()&&!ops&&i9100_ui_execution_diag[8]==7);
 reset();p.p_uuid[0]^=1;i9100_ui_execution_record(&p,0x65000);assert(!i9100_ui_execution_pending[0]);
 for(unsigned n=0;n<9;n++){reset();uint8_t*original=n==8?exe+i9100_ui_execution_sites[n]:image+(i9100_ui_execution_sites[n]-0x32f2f000);original[0]^=1;i9100_ui_execution_record(&p,0x65000);assert(!bootstrap()&&!ops&&i9100_ui_execution_diag[8]==1);}
 reset();install();for(unsigned n=1;n<=9;n++){saved[4]=n==2?3:0x40000;i9100_ui_execution_note(&p,0x1000+(n-1)*64+22,0x30000,n);volatile uint32_t*r=&i9100_ui_execution_rows[4+(n-1)*96];assert(r[0]==n&&r[4]==n&&!r[7]&&!r[12]&&!r[14]&&r[13]==(n==1?116:n==2?80:n==8?55:192)&&r[15]==(n==2||n==8?0:n==1||(n>=5&&n<=7)?128:32));assert(i9100_ui_execution_diag[n<=3?15+n:16+n]==1);}
 unsigned count=i9100_ui_execution_rows[0];i9100_ui_execution_note(&p,0x1018,0x30000,1);assert(i9100_ui_execution_rows[0]==count);
 read_fail=0x30000;i9100_ui_execution_note(&p,0x1016,0x30000,1);assert(i9100_ui_execution_rows[4+9*96+7]==14);read_fail=0;
 for(unsigned n=0;n<80;n++)i9100_ui_execution_note(&p,0x1056,0x30000,2);assert(i9100_ui_execution_rows[0]==90&&i9100_ui_execution_diag[17]==81);
 char path[1024];snprintf(path,sizeof(path),"%s.rows",argv[1]);f=fopen(path,"wb");assert(f);fwrite((const void*)i9100_ui_execution_rows,1,sizeof(i9100_ui_execution_rows),f);fclose(f);
 snprintf(path,sizeof(path),"%s.diag",argv[1]);f=fopen(path,"wb");assert(f);fwrite((const void*)i9100_ui_execution_diag,1,sizeof(i9100_ui_execution_diag),f);fclose(f);
 unsigned last=i9100_ui_execution_rows[0];read_fail=0x20000+728;i9100_ui_execution_note(&p,0x1116,0x30000,5);
 assert(i9100_ui_execution_rows[0]==last+1&&i9100_ui_execution_rows[4+((last)&63)*96+14]==14);
 reset();retina=1;install();assert(i9100_ui_execution_diag[26]==1&&memcmp(image+(i9100_ui_execution_sites[9]-0x32f2f000),shared+(i9100_ui_execution_sites[9]-0x32f2f000),8));
 snprintf(path,sizeof(path),"%s.retina",argv[1]);f=fopen(path,"wb");assert(f);fwrite(codebuf+576,1,80,f);fclose(f);
 i9100_ui_retina_code(codebuf,0x2d000,0x20c000);
 snprintf(path,sizeof(path),"%s.retina-slide",argv[1]);f=fopen(path,"wb");assert(f);fwrite(codebuf,1,80,f);fclose(f);
 for(unsigned n=1;n<=33;n++){reset();retina=1;i9100_ui_execution_record(&p,0x65000);fail_at=n;assert(!bootstrap()&&i9100_ui_execution_diag[1]==3&&!memcmp(image,shared,sizeof(image))&&!memcmp(exe,exeshared,sizeof(exe)));}
 reset();retina=1;read_fail=0x3310e9c4;i9100_ui_execution_record(&p,0x65000);assert(!bootstrap()&&!ops&&i9100_ui_execution_diag[8]==8);
 puts("PASS production own-task Setup observer, exact UUID/bytes refusal,30 partial-write transaction rollbacks,unsafe stop,receiver/event bytes,scope and ring wrap");
}
'''
(out/'check.c').write_text(source)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',out/'check.c','-o',out/'check'],check=True)
subprocess.run([out/'check',out/'code.bin'],check=True)
sys.path.insert(0,str(repo.parent/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *
regs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_CPSR]
sites=(0x32f3284c,0x32f439cc,0x32f44d58,0x32f453e4,0x33074028,0x3307604c,0x33076ee4,0x330185c0,0x73f18)
original=[bytes.fromhex(x) for x in ('f0b503af2de9000d','4df26c51c0f28a61','f0b503af4df8048d','f0b503af2de9000d','f0b503af2de9000d','f0b503af2de9000d','f0b503af4df8048d','f0b549f6a6260446','f0b503af2de9000d')];code=(out/'code.bin').read_bytes()
for n,site in enumerate(sites):
    results=[]
    for traced in (False,True):
        u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);u.mem_map(site&~4095,4096);u.mem_map(0x1000,4096);u.mem_map(0x20000000,0x20000)
        u.mem_write(site,struct.pack('<2I',0xf000f8df,0x1000+n*64+1) if traced else original[n]);u.mem_write(0x1000,code)
        u.reg_write(UC_ARM_REG_CPSR,0xb80f0030)
        for i,r in enumerate(regs[:13]):u.reg_write(r,0x7000+i)
        u.reg_write(UC_ARM_REG_SP,0x20010000);u.reg_write(UC_ARM_REG_LR,0x2001ff01);calls=[]
        def hook(uc,at,size,arg):
            if at==site+8:uc.emu_stop()
            if traced and at==0x1000+n*64+20:
                assert uc.reg_read(UC_ARM_REG_R0)==n+1 and uc.reg_read(UC_ARM_REG_R12)==20
                calls.append(at);uc.reg_write(UC_ARM_REG_R0,104);uc.reg_write(UC_ARM_REG_R1,1);uc.reg_write(UC_ARM_REG_CPSR,uc.reg_read(UC_ARM_REG_CPSR)^(1<<29));uc.reg_write(UC_ARM_REG_PC,(at+2)|1)
        u.hook_add(UC_HOOK_CODE,hook);u.emu_start(site|1,site+12,count=1000)
        values=[u.reg_read(r) for r in regs];sp=values[13];results.append((values,bytes(u.mem_read(sp,0x20010000-sp))))
        assert len(calls)==int(traced)
    assert results[0]==results[1],n
(out/'result.json').write_text(json.dumps(dict(result='PASS',production=True,nine_actual_cortex_a9_prologues=True,scope='Setup only; observation does not rewrite UI data or returns'),indent=2)+'\n')
print('PASS actual Cortex-A9 Setup/UIKit prologues preserve original registers,flags,stack and future PC-relative instruction location')
for slide,header,suffix in ((0,0x65000,'.retina'),(0x20c000,0x2d000,'.retina-slide')):
    retina_code=(out/('code.bin'+suffix)).read_bytes()
    for name in (header+0x62544,0x77774):
        u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        for page in (0x1000,0x32f85e94&~4095,0x3310e9c4&~4095,0x32f888d8&~4095,0x32f4db30&~4095):
            if page!=0x1000:page+=slide
            u.mem_map(page,4096)
        u.mem_map(0x20000000,0x20000);u.mem_write(0x1240,retina_code)
        u.mem_write(0x32f85e94+slide,struct.pack('<2I',0xf000f8df,0x1241))
        u.mem_write(0x3310e9c4+slide,bytes.fromhex('104679f687bf00bf'))
        u.reg_write(UC_ARM_REG_CPSR,0xb80f0030)
        for i,r in enumerate(regs[:13]):u.reg_write(r,0x7000+i)
        u.reg_write(UC_ARM_REG_R0,0x397e78b0+slide);u.reg_write(UC_ARM_REG_R1,0x333ae11a+slide);u.reg_write(UC_ARM_REG_R2,name)
        u.reg_write(UC_ARM_REG_SP,0x20010000);u.reg_write(UC_ARM_REG_LR,0x2001ff01)
        expected=[u.reg_read(r) for r in regs];calls=[]
        def asset_hook(uc,at,size,arg):
            if at==0x3310e9c4+slide:
                assert uc.reg_read(UC_ARM_REG_R0)==0x397e78b0+slide
                assert uc.reg_read(UC_ARM_REG_R1)==0x333ae22c+slide and uc.reg_read(UC_ARM_REG_R2)==0x40000000
            if at==0x32f888d8+slide:
                assert uc.reg_read(UC_ARM_REG_R0)==0x40000000
                calls.append(at)
                for r in regs[:4]+[UC_ARM_REG_R12]:uc.reg_write(r,0xbad)
                uc.reg_write(UC_ARM_REG_CPSR,uc.reg_read(UC_ARM_REG_CPSR)^(1<<29))
                uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
            if at==0x32f4db30+slide:uc.emu_stop()
        u.hook_add(UC_HOOK_CODE,asset_hook);u.emu_start(0x32f85e95+slide,0x32f85e94+slide+16,count=1000)
        actual=[u.reg_read(r) for r in regs]
        expected[0]=name
        assert actual==expected and len(calls)==int(name==header+0x62544)
print('PASS exact Next-name guard, original UIImage setter ABI/tail branch, caller registers/flags/stack and original loader resume; image results require hardware')
print('EVIDENCE',out)
