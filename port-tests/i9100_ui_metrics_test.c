#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UInt32,vm_map_offset_t,vm_map_size_t;
typedef uint64_t user_addr_t,vm_object_offset_t;
typedef void *vm_object_t;typedef int vm_prot_t,vm_inherit_t,kern_return_t;
typedef unsigned mach_msg_type_number_t;typedef int *task_info_t;
typedef struct {uint64_t all_image_info_addr,all_image_info_size;int all_image_info_format;}task_dyld_info_data_t;
struct task{unsigned acts;};typedef struct task*task_t;
struct proc{const char*p_comm;unsigned p_pid;task_t task;uint8_t p_uuid[16];unsigned p_idversion;};typedef struct proc*proc_t;
typedef int*vm_map_t;
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
#define OSMemoryBarrier() __sync_synchronize()
#define I9100_DENSITY_TEST_SYNC() do{syncs++;}while(0)
static int map;static struct task child={1},other={1};static task_t current=&child;
static struct proc p;static unsigned option=1333,enabled=1,abi=6,native=1,irq,interrupts=1,preempt;
static unsigned step,fail,again,allocated,removed,syncs,writable,slide;
static uint8_t ui_image[64],gs_image[64],original[12],code[276];static uint32_t dyld[21],saved[16],object[8];
static task_t current_task(void){return current;}static vm_map_t current_map(void){return &map;}
static vm_map_t get_task_map(task_t t){return t==&child?&map:NULL;}
static unsigned get_task_numacts(task_t t){return t->acts;}
static unsigned ml_at_interrupt_context(void){return irq;}static unsigned ml_get_interrupts_enabled(void){return interrupts;}
static unsigned get_preemption_level(void){return preempt;}
static int PE_parse_boot_argn(const char*k,int*v,unsigned n){assert(n==4);*v=!strcmp(k,"i9100uidensitymetrics")?enabled:!strcmp(k,"i9100uidensity")?option:!strcmp(k,"i9100hidabi")?abi:native;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned next,volatile unsigned*v){if(*v!=old)return 0;*v=next;return 1;}
static int task_info(task_t t,unsigned f,task_info_t out,unsigned*n){assert(t==&child && f==17 && *n==TASK_DYLD_INFO_COUNT);task_dyld_info_data_t*i=(void*)out;i->all_image_info_addr=0x25000;i->all_image_info_size=84;i->all_image_info_format=0;return 0;}
static int copyin(user_addr_t a,void*out,unsigned n){
 assert(current==&child && !irq && interrupts && !preempt);
 if(a==0x25000 && n==84)memcpy(out,dyld,n);
 else if(a>=0x32f2f000U+slide && a+n<=0x32f2f040U+slide)memcpy(out,ui_image+(a-0x32f2f000U-slide),n);
 else if(a>=0x34c2f000U+slide && a+n<=0x34c2f040U+slide)memcpy(out,gs_image+(a-0x34c2f000U-slide),n);
 else if(a==0x32f882dcU+slide && n==12)memcpy(out,original,n);
 else if(a==0x27000 && n==64)memcpy(out,saved,n);
 else if(a==0x29000 && n==32)memcpy(out,object,n);
 else {fprintf(stderr,"unexpected read %llx/%u\n",(unsigned long long)a,n);assert(0);}
 return 0;
}
static int bad(void){++step;return step==fail || step==again;}
static kern_return_t vm_map_enter(vm_map_t m,unsigned*a,unsigned n,unsigned mask,unsigned flags,void*o,uint64_t off,int copy,unsigned prot,unsigned max,unsigned inh){assert(m==&map && n==4096 && !mask && flags==1 && !o && !off && !copy && prot==3 && max==7 && inh==1);if(bad())return 4;*a=0x1000;allocated=1;return 0;}
static kern_return_t vm_map_protect(vm_map_t m,unsigned a,unsigned b,unsigned prot,int maximum){assert(m==&map && !maximum && b==a+4096);assert(a==0x1000 || a==(0x32f88000U+slide));if(bad())return 4;if(a!=0x1000)writable=prot==19;return 0;}
static int copyout(const void*in,unsigned a,unsigned n){int e=bad();if(a==0x1000){assert(n==276 && allocated);memcpy(code,in,e?n/2:n);}else{assert(a==0x32f882dcU+slide && n==12 && writable);memcpy(original,in,e?n/2:n);}return e?14:0;}
static void vm_map_remove(vm_map_t m,unsigned a,unsigned b,unsigned f){assert(m==&map && a==0x1000 && b==0x2000 && !f);removed++;}
static void flush_dcache(unsigned a,unsigned n,int physical){assert(!physical && ((a==0x1000 && n==276) || (a==0x32f882dcU+slide && n==12)));}
static void invalidate_icache(unsigned a,unsigned n,int physical){flush_dcache(a,n,physical);}
#include "production.h"
static void image(uint8_t*bytes,const uint8_t*uuid){uint32_t h[7]={0xfeedface,12,0,6,1,24,0},lc[2]={27,24};memset(bytes,0,64);memcpy(bytes,h,28);memcpy(bytes+28,lc,8);memcpy(bytes+36,uuid,16);}
static void reset(unsigned slot){
 memset((void*)i9100_ui_density_diag,0,sizeof(i9100_ui_density_diag));memset((void*)i9100_ui_density_execution,0,sizeof(i9100_ui_density_execution));
 memset((void*)i9100_ui_density_admission,0,sizeof(i9100_ui_density_admission));memset(i9100_ui_density_pending,0,sizeof(i9100_ui_density_pending));memset((void*)i9100_ui_density_guard,0,sizeof(i9100_ui_density_guard));
 current=&child;child.acts=1;option=1333;enabled=1;abi=6;native=1;irq=preempt=step=fail=again=allocated=removed=syncs=writable=slide=0;interrupts=1;
 p.p_comm=i9100_ui_density_names[slot];p.p_pid=slot+8;p.p_idversion=slot+8;p.task=&child;memcpy(p.p_uuid,i9100_ui_density_uuids[slot],16);
 static const uint8_t uuid[16]={0xad,0x8b,0x3a,0xd2,0x3f,0x41,0x31,0x87,0xa1,0x78,0x17,0x9d,0xb3,0x9c,0xfa,0x6b};image(ui_image,uuid);image(gs_image,i9100_ui_density_gs_uuid);memcpy(original,i9100_metrics_original,12);
 memset(dyld,0,sizeof(dyld));dyld[0]=12;memset(saved,0,sizeof(saved));saved[6]=0x29000;saved[15]=0x30003331;memset(object,0,sizeof(object));object[0]=0x397e9534;object[6]=0x3f800000;
}
static int boot(void){i9100_ui_density_record(&p,0x65000);return i9100_ui_density_bootstrap(&p,0x30001000,0x10);}
int main(int argc,char**argv){assert(argc==2);
 for(unsigned slot=1;slot<3;slot++){reset(slot);assert(!boot() && i9100_ui_density_diag[slot][0]==4 && i9100_ui_density_diag[slot][1]==2 && !removed && !writable);uint32_t jump[3];memcpy(jump,original,12);assert(jump[0]==0xf000f8df && jump[1]==0x1001 && jump[2]==0xbf00bf00);
 i9100_ui_density_note(&p,0x1016,0x27000,1);assert(i9100_ui_density_execution[slot][3]==1 && !i9100_ui_density_execution[slot][6]);object[6]=0x3faaaaab;i9100_ui_density_note(&p,0x10f8,0x27000,2);assert(i9100_ui_density_execution[slot][4]==1 && i9100_ui_density_execution[slot][10]==0x3faaaaab);}
 for(unsigned n=1;n<=6;n++){reset(1);fail=n;assert(!boot() && i9100_ui_density_diag[1][1]==3 && !memcmp(original,i9100_metrics_original,12));assert(!allocated || removed);}
 reset(1);fail=5;again=8;assert(boot()==EIO && !removed);
 for(unsigned test=0;test<9;test++){reset(1);if(test==0)enabled=0;if(test==1)abi=7;if(test==2)native=0;if(test==3)p.p_uuid[0]^=1;if(test==4)current=&other;if(test==5)child.acts=2;if(test==6)irq=1;if(test==7)interrupts=0;if(test==8)preempt=1;i9100_ui_density_record(&p,0x65000);assert(!i9100_metrics_bootstrap(&p,0x30001000,0x10) && !step);}
 reset(0);assert(!boot() && !step);
 for(unsigned t=0;t<3;t++){reset(1);if(t==0)ui_image[36]^=1;if(t==1)gs_image[36]^=1;if(t==2)original[0]^=1;assert(!boot() && !step && i9100_ui_density_diag[1][1]==3);}
 for(unsigned v=1333;v<=1500;v+=167)for(unsigned r=0;r<2;r++){reset(1);unsigned relocate=r?0x0375b000U:0;i9100_metrics_code(code,relocate,v);char path[512];snprintf(path,sizeof(path),"%s/code-%u-%08x.bin",argv[1],v,relocate);FILE*f=fopen(path,"wb");assert(f && fwrite(code,1,sizeof(code),f)==sizeof(code));fclose(f);}
 puts("PASS original metrics publication patch, native observation scope, partial writes/rollback, unsafe-target retention and UUID/ABI/context/original bytes");
}
