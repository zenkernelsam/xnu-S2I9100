#ifndef I9100_SLAB_PUBLICATION_TRACE_H
#define I9100_SLAB_PUBLICATION_TRACE_H
#include <vm/vm_map.h>
#include <vm/pmap.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>
#include <mach/task.h>
/* BSD exports omit this private prototype; its object offset is64-bit. */
extern kern_return_t vm_map_enter(vm_map_t,vm_map_offset_t *,vm_map_size_t,
    vm_map_offset_t,int,vm_object_t,vm_object_offset_t,boolean_t,
    vm_prot_t,vm_prot_t,vm_inherit_t);

/* Optional own-task observation immediately before QuartzCore links a new
 * slab into its bucket. Real instructions replay unchanged after real getpid;
 * no allocator result/header/magic is replaced. */
volatile uint32_t i9100_slab_publication_diag[32]={1};
volatile uint32_t i9100_slab_publication_rows[4+64*32];
volatile uint32_t i9100_slab_failure_mapping_diag[16]={1};
static volatile UInt32 i9100_slab_publication_guard;
static uint32_t i9100_slab_publication_pending[4];
static const uint8_t i9100_slab_publication_uuid[16]={
0xe1,0xdc,0x74,0x43,0x4e,0x0e,0x39,0x38,0xb1,0x7f,0xf0,0xf7,0xad,0x85,0xd1,0x38};
/* The absolute Thumb literal branch requires a word-aligned site. Start at
 * the preceding ldr r0,[r3], replay that load and the first three stores;
 * return to 2468 for the final original store. The loop target 246a is intact. */
static const uint32_t i9100_slab_publication_sites[1]={0x32ce2460};
static const uint8_t i9100_slab_publication_width[1]={8};
static const uint8_t i9100_slab_publication_original[1][12]={{0x18,0x68,0x1d,0x60,0x2e,0x60,0x05,0x60}};
static const uint8_t i9100_slab_publication_cache_uuid[1][16]={{0xc0,0x86,0xb6,0xe6,0xcd,0x9d,0x34,0x13,0x99,0xbc,0xc3,0x67,0x5c,0x82,0xf1,0xfe}};
static const uint8_t i9100_slab_publication_prefix[36]={
    0x2d,0xe9,0xff,0x5f,0xef,0xf3,0x00,0x8c,0x82,0xb0,0xcd,0xf8,0x00,0xc0,
    0x01,0x20,0x4f,0xf0,0x14,0x0c,0x80,0xdf,0xdd,0xf8,0x00,0xc0,0x02,0xb0,
    0x8c,0xf3,0x00,0x8c,0xbd,0xe8,0xff,0x5f};

static void i9100_slab_publication_record(proc_t p,user_addr_t header)
{
    int enabled=0;
    if(!p || !p->task || strcmp(p->p_comm,"backboardd") ||
       !PE_parse_boot_argn("i9100slabtrace",&enabled,sizeof(enabled)) || enabled!=1 ||
       memcmp(p->p_uuid,i9100_slab_publication_uuid,16) ||
       header<4096 || header>0x40000000U-0x2a4000U || (header&4095U))return;
    if(!OSCompareAndSwap(0,1,&i9100_slab_publication_guard))return;
    i9100_slab_publication_pending[0]=p->p_pid;
    i9100_slab_publication_pending[1]=(uint32_t)(uintptr_t)p->task;
    i9100_slab_publication_pending[2]=(uint32_t)header;
    i9100_slab_publication_pending[3]=p->p_idversion;
    OSMemoryBarrier();i9100_slab_publication_guard=0;
}

static int i9100_slab_publication_image_matches(uint32_t header,const uint8_t *uuid)
{
    uint32_t mh[7],command[2];uint8_t actual[16];
    if(header<4096 || header>0x3fffb000U || (header&4095U) ||
       copyin(header,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 ||
       mh[4]>128 || mh[5]>0x4000U || mh[5]<24)return 0;
    uint32_t at=header+28,end=at+mh[5];
    for(unsigned n=0;n<mh[4];n++){
        if(at>end-8U || copyin(at,command,8) || command[1]<8 || command[1]>end-at)return 0;
        if(command[0]==27){
            return command[1]==24 && !copyin(at+8,actual,16) && !memcmp(actual,uuid,16);
        }
        at+=command[1];
    }
    return 0;
}

int i9100_slab_publication_bootstrap(proc_t p,unsigned return_pc,unsigned return_cpsr)
{
    int enabled=0,abi=0;
    if(!p || strcmp(p->p_comm,"backboardd") ||
       !PE_parse_boot_argn("i9100slabtrace",&enabled,sizeof(enabled)) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,sizeof(abi)) || abi!=6 ||
       !p->task || current_task()!=(task_t)p->task ||
       current_map()!=get_task_map((task_t)p->task) || get_task_numacts((task_t)p->task)!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return 0;
    if(!OSCompareAndSwap(0,1,&i9100_slab_publication_guard))return 0;
    volatile uint32_t *d=i9100_slab_publication_diag;
    if(d[6]>=8 || i9100_slab_publication_pending[0]!=(uint32_t)p->p_pid ||
       i9100_slab_publication_pending[1]!=(uint32_t)(uintptr_t)p->task ||
       i9100_slab_publication_pending[3]!=(uint32_t)p->p_idversion){i9100_slab_publication_guard=0;return 0;}
    uint32_t header=i9100_slab_publication_pending[2];memset(i9100_slab_publication_pending,0,16);
    d[6]++;d[1]=1;d[2]=p->p_pid;d[3]=(uint32_t)(uintptr_t)p->task;d[4]=header;d[12]=p->p_idversion;
    d[7]=d[8]=d[9]=0;d[11]=1;
    vm_map_t map=current_map();vm_map_offset_t address=4096,sites[1];uint32_t mh[7],dyld[21];
    d[13]=(uint32_t)(uintptr_t)map;d[14]=(uint32_t)(uintptr_t)map->pmap;
    uint8_t original[1][12],code[64]={0};unsigned touched=0;boolean_t allocated=FALSE;int error=0;
    if(memcmp(p->p_uuid,i9100_slab_publication_uuid,16) || header<4096 || (header&4095U) ||
       header>0x40000000U-0x2a4000U || copyin(header,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[3]!=2)goto failed;
    task_dyld_info_data_t info={0};mach_msg_type_number_t info_count=TASK_DYLD_INFO_COUNT;
    if(return_pc<4096 || return_pc>0x3fffffffU || (return_cpsr&31U)!=16 ||
       task_info(current_task(),TASK_DYLD_INFO,(task_info_t)&info,&info_count)!=KERN_SUCCESS ||
       info_count!=TASK_DYLD_INFO_COUNT || info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_32 ||
       info.all_image_info_addr<4096 || info.all_image_info_addr>0x3fffffacU ||
       info.all_image_info_size<84 || copyin(info.all_image_info_addr,dyld,84) ||
       dyld[0]<12 || dyld[0]>15 || (dyld[20]&4095U) || dyld[20]>0x02800000U){d[8]=6;goto failed;}
    uint32_t slide=dyld[20];d[31]=slide;
    if(!i9100_slab_publication_image_matches(0x32cdc000U+slide,i9100_slab_publication_cache_uuid[0])){d[8]=7;goto failed;}
    for(unsigned n=0;n<1;n++){
        sites[n]=i9100_slab_publication_sites[n]+slide;
        if(copyin(sites[n],original[n],i9100_slab_publication_width[n]) ||
           memcmp(original[n],i9100_slab_publication_original[n],i9100_slab_publication_width[n])){d[8]=1;goto failed;}
    }
    d[11]=2;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;allocated=TRUE;d[5]=address;
    if(address<4096 || address>0x3ffff000U)goto failed;
    memcpy(code,i9100_slab_publication_prefix,36);
    memcpy(code+36,original[0],8);
    uint32_t jump[2]={0xf000f8dfU,sites[0]+9U};memcpy(code+44,jump,8);
    if(copyout(code,address,sizeof(code))){d[8]=2;goto failed;}
    flush_dcache(address,sizeof(code),FALSE);invalidate_icache(address,sizeof(code),FALSE);
    result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;d[11]=3;
    for(unsigned n=0;n<1;n++){
        uint32_t page=sites[n]&~4095U;
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
        uint32_t branch[2]={0xf000f8dfU,address+n*64U+1U};touched|=1U<<n;
        if(copyout(branch,sites[n],8)){d[8]=3+n;goto failed;}
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
    }
    d[10]++;d[11]=4;d[30]=sites[0];
    OSMemoryBarrier();d[1]=2;i9100_slab_publication_guard=0;return 0;
failed:
    for(unsigned n=0;n<1;n++)if(touched&(1U<<n)){
        uint32_t page=sites[n]&~4095U;
        kern_return_t restore=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
        if(restore==KERN_SUCCESS)restore=copyout(original[n],sites[n],8);
        if(restore==KERN_SUCCESS)touched&=~(1U<<n);
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        kern_return_t protect=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
        if(restore!=KERN_SUCCESS || protect!=KERN_SUCCESS){d[9]=restore?restore:protect;error=EIO;}
    }
    if(allocated && !touched)vm_map_remove(map,address,address+4096,0);
    OSMemoryBarrier();d[1]=3;i9100_slab_publication_guard=0;return error;
}

void i9100_slab_publication_note(proc_t p,unsigned pc,unsigned sp)
{
    volatile uint32_t *d=i9100_slab_publication_diag;
    if(!p || d[1]!=2 || strcmp(p->p_comm,"backboardd") ||
       d[2]!=(uint32_t)p->p_pid || d[3]!=(uint32_t)(uintptr_t)current_task() ||
       p->task!=current_task() || current_map()!=get_task_map(p->task) ||
       memcmp(p->p_uuid,i9100_slab_publication_uuid,16) ||
       d[12]!=(uint32_t)p->p_idversion || pc!=d[5]+22U ||
       sp<4096 || sp>0x3fffff94U || (sp&3U) || ml_at_interrupt_context() ||
       !ml_get_interrupts_enabled() || get_preemption_level()!=0)return;
    uint32_t saved[16]={0},header[8]={0},expected=0;
    unsigned stack_error=copyin(sp,saved,64);
    unsigned expected_error=stack_error?stack_error:copyin(sp+104U,&expected,4);
    unsigned candidate=saved[7],link=saved[5],read_error=UINT32_MAX;
    unsigned before=0,after=0;vm_map_t map=current_map();
    if(!stack_error && candidate>=4096 && candidate<=0x3fffffe0U && !(candidate&3U)){
        before=pmap_find_phys(map->pmap,candidate);
        read_error=copyin(candidate,header,32);
        after=pmap_find_phys(map->pmap,candidate);
    }
    if(d[1]!=2 || d[2]!=(unsigned)p->p_pid || d[3]!=(unsigned)(uintptr_t)current_task() || d[12]!=p->p_idversion)return;
    if(!OSCompareAndSwap(0,1,&i9100_slab_publication_guard))return;
    if(d[1]!=2 || d[2]!=(unsigned)p->p_pid || d[3]!=(unsigned)(uintptr_t)current_task() || d[12]!=p->p_idversion){
        i9100_slab_publication_guard=0;return;
    }
    unsigned count=i9100_slab_publication_rows[0];
    if(count<64){
        volatile uint32_t *r=&i9100_slab_publication_rows[4+count*32];
        memset((void*)r,0,128);
        r[1]=p->p_pid;r[2]=p->p_idversion;r[3]=d[3];r[4]=candidate;r[5]=link;
        r[6]=expected;r[7]=header[4];r[8]=stack_error;r[9]=read_error;r[10]=expected_error;
        r[11]=before;r[12]=after;r[13]=pc;r[14]=sp;r[15]=(uint32_t)(uintptr_t)map;
        memcpy((void*)&r[16],header,32);memcpy((void*)&r[24],saved+2,32);
        i9100_slab_publication_rows[1]=1;i9100_slab_publication_rows[2]=64;i9100_slab_publication_rows[3]=32;
        OSMemoryBarrier();r[0]=count+1;i9100_slab_publication_rows[0]=count+1;
    }else if(d[17]!=UINT32_MAX)d[17]++;
    if(d[16]!=UINT32_MAX)d[16]++;
    OSMemoryBarrier();i9100_slab_publication_guard=0;
}

/* Called from ordinary self-task SIGABRT capture, after signal/proc locks
 * have been released. PPNs are snapshots, not proof of mapping lifetime. */
void i9100_slab_publication_failure(proc_t p,unsigned candidate)
{
    volatile uint32_t *d=i9100_slab_publication_diag,*r=i9100_slab_failure_mapping_diag;
    if(!p || d[1]!=2 || r[1] || strcmp(p->p_comm,"backboardd") ||
       d[2]!=(uint32_t)p->p_pid || d[3]!=(uint32_t)(uintptr_t)current_task() ||
       p->task!=current_task() || d[12]!=p->p_idversion ||
       memcmp(p->p_uuid,i9100_slab_publication_uuid,16) ||
       current_map()!=get_task_map(p->task) || candidate<4096 ||
       candidate>0x3fffffe0U || (candidate&3U) || ml_at_interrupt_context() ||
       !ml_get_interrupts_enabled() || get_preemption_level()!=0)return;
    vm_map_t map=current_map();uint32_t header[8]={0};
    unsigned before=pmap_find_phys(map->pmap,candidate);
    unsigned error=copyin(candidate,header,32);
    unsigned after=pmap_find_phys(map->pmap,candidate);
    if(!OSCompareAndSwap(0,1,&i9100_slab_publication_guard))return;
    if(!r[1] && d[1]==2 && d[2]==(uint32_t)p->p_pid && d[3]==(uint32_t)(uintptr_t)current_task() && d[12]==p->p_idversion){
        r[2]=p->p_pid;r[3]=p->p_idversion;r[4]=d[3];r[5]=candidate;
        r[6]=(uint32_t)(uintptr_t)map;r[7]=(uint32_t)(uintptr_t)map->pmap;
        r[8]=before;r[9]=after;r[10]=header[4];r[11]=error;r[12]=d[10];r[13]=d[16];r[14]=d[30];r[15]=1;
        OSMemoryBarrier();r[1]=1;
    }
    OSMemoryBarrier();i9100_slab_publication_guard=0;
}
#endif
