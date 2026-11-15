#ifndef I9100_UI_SOUNDS_UNAVAILABLE_H
#define I9100_UI_SOUNDS_UNAVAILABLE_H
#include <vm/vm_map.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>
#include <mach/task.h>
/* BSD exports omit this private prototype; its object offset is64-bit. */
extern kern_return_t vm_map_enter(vm_map_t,vm_map_offset_t *,vm_map_size_t,
    vm_map_offset_t,int,vm_object_t,vm_object_offset_t,boolean_t,
    vm_prot_t,vm_prot_t,vm_inherit_t);

/* Explicit development-only system-sound unavailability for the exact iOS6
 * SpringBoard. OSStatus APIs return -1500; void playback is muted. No sound
 * ID is created, no success is claimed, and other processes retain their APIs.
 * getpid is real; the prefix restores arguments/registers/flags before the
 * deliberately unavailable API returns. Calls are counted at exact SVC PCs. */
volatile uint32_t i9100_ui_sounds_unavailable_diag[32]={1};
static volatile UInt32 i9100_ui_sounds_unavailable_guard;
static uint32_t i9100_ui_sounds_unavailable_pending[4];
static const uint8_t i9100_ui_sounds_unavailable_uuid[16]={
    0xbd,0x1d,0x77,0xd1,0x54,0xef,0x3d,0xff,0xbb,0x53,0xd8,0x93,0x37,0xc8,0xa0,0x8f};
static const uint32_t i9100_ui_sounds_unavailable_sites[9]={0x30bd5cd4,0x30bd5744,0x30bd5050,0x30bd71e8,0x30bd7598,0x30bd6364,0x30bd65e0,0x30bd6704,0x30bd6c6c};
static const uint8_t i9100_ui_sounds_unavailable_width[9]={8,8,8,8,8,8,8,8,8};
static const uint8_t i9100_ui_sounds_unavailable_original[9][12]={
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0x90,0xb5,0x01,0xaf,0x81,0xb0,0x04,0x46},
    {0x90,0xb5,0x01,0xaf,0x81,0xb0,0x04,0x46},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x05},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d}};
static const uint8_t i9100_ui_sounds_unavailable_cache_uuid[1][16]={
    {0x39,0x4e,0xe1,0x1c,0xf8,0x26,0x36,0x7d,0xb9,0xff,0x49,0x68,0xdb,0xc7,0x1d,0x6d}};
/* MOVW r0,#0xfa24; MOVT r0,#0xffff; BX lr = the original SDK's
 * kAudioServicesSystemSoundUnspecifiedError(-1500). Error outputs are left
 * untouched: an error does not provide an output sound ID/property value. */
static const uint8_t i9100_ui_sounds_unavailable_error[10]={
    0x4f,0xf6,0x24,0x20,0xcf,0xf6,0xff,0x70,0x70,0x47};
static const uint8_t i9100_ui_sounds_unavailable_mute[2]={0x70,0x47};
static const uint8_t i9100_ui_sounds_unavailable_prefix[36]={
    0x2d,0xe9,0xff,0x5f,0xef,0xf3,0x00,0x8c,0x82,0xb0,0xcd,0xf8,0x00,0xc0,
    0x01,0x20,0x4f,0xf0,0x14,0x0c,0x80,0xdf,0xdd,0xf8,0x00,0xc0,0x02,0xb0,
    0x8c,0xf3,0x00,0x8c,0xbd,0xe8,0xff,0x5f};

static void i9100_ui_sounds_unavailable_record(proc_t p,user_addr_t header)
{
    int enabled=0;
    if(!p || !p->task || strcmp(p->p_comm,"SpringBoard") ||
       !PE_parse_boot_argn("i9100nosound",&enabled,sizeof(enabled)) || enabled!=1 ||
       memcmp(p->p_uuid,i9100_ui_sounds_unavailable_uuid,16) ||
       header<4096 || header>0x40000000U-0x2a4000U || (header&4095U))return;
    if(!OSCompareAndSwap(0,1,&i9100_ui_sounds_unavailable_guard))return;
    i9100_ui_sounds_unavailable_pending[0]=p->p_pid;
    i9100_ui_sounds_unavailable_pending[1]=(uint32_t)(uintptr_t)p->task;
    i9100_ui_sounds_unavailable_pending[2]=(uint32_t)header;
    i9100_ui_sounds_unavailable_pending[3]=p->p_idversion;
    OSMemoryBarrier();i9100_ui_sounds_unavailable_guard=0;
}

static int i9100_ui_sounds_unavailable_image_matches(uint32_t header,const uint8_t *uuid)
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

int i9100_ui_sounds_unavailable_bootstrap(proc_t p,unsigned return_pc,unsigned return_cpsr)
{
    int enabled=0,abi=0;
    if(!p || strcmp(p->p_comm,"SpringBoard") ||
       !PE_parse_boot_argn("i9100nosound",&enabled,sizeof(enabled)) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,sizeof(abi)) || abi!=6 ||
       !p->task || current_task()!=(task_t)p->task ||
       current_map()!=get_task_map((task_t)p->task) || get_task_numacts((task_t)p->task)!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return 0;
    if(!OSCompareAndSwap(0,1,&i9100_ui_sounds_unavailable_guard))return 0;
    volatile uint32_t *d=i9100_ui_sounds_unavailable_diag;
    if(d[6]>=8 || i9100_ui_sounds_unavailable_pending[0]!=(uint32_t)p->p_pid ||
       i9100_ui_sounds_unavailable_pending[1]!=(uint32_t)(uintptr_t)p->task ||
       i9100_ui_sounds_unavailable_pending[3]!=(uint32_t)p->p_idversion){i9100_ui_sounds_unavailable_guard=0;return 0;}
    uint32_t header=i9100_ui_sounds_unavailable_pending[2];memset(i9100_ui_sounds_unavailable_pending,0,16);
    d[6]++;d[1]=1;d[2]=p->p_pid;d[3]=(uint32_t)(uintptr_t)p->task;d[4]=header;d[12]=p->p_idversion;
    d[7]=d[8]=d[9]=0;d[11]=1;
    vm_map_t map=current_map();vm_map_offset_t address=4096,sites[9];uint32_t mh[7],dyld[21];
    uint8_t original[9][12],code[576]={0};unsigned touched=0;boolean_t allocated=FALSE;int error=0;
    if(memcmp(p->p_uuid,i9100_ui_sounds_unavailable_uuid,16) || header<4096 || (header&4095U) ||
       header>0x40000000U-0x2a4000U || copyin(header,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[3]!=2)goto failed;
    task_dyld_info_data_t info={0};mach_msg_type_number_t info_count=TASK_DYLD_INFO_COUNT;
    if(return_pc<4096 || return_pc>0x3fffffffU || (return_cpsr&31U)!=16 ||
       task_info(current_task(),TASK_DYLD_INFO,(task_info_t)&info,&info_count)!=KERN_SUCCESS ||
       info_count!=TASK_DYLD_INFO_COUNT || info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_32 ||
       info.all_image_info_addr<4096 || info.all_image_info_addr>0x3fffffacU ||
       info.all_image_info_size<84 || copyin(info.all_image_info_addr,dyld,84) ||
       dyld[0]<12 || dyld[0]>15 || (dyld[20]&4095U) || dyld[20]>0x02800000U){d[8]=6;goto failed;}
    uint32_t slide=dyld[20];d[31]=slide;
    if(!i9100_ui_sounds_unavailable_image_matches(0x30af6000U+slide,i9100_ui_sounds_unavailable_cache_uuid[0])){d[8]=7;goto failed;}
    for(unsigned n=0;n<9;n++){
        sites[n]=i9100_ui_sounds_unavailable_sites[n]+slide;
        if(copyin(sites[n],original[n],i9100_ui_sounds_unavailable_width[n]) ||
           memcmp(original[n],i9100_ui_sounds_unavailable_original[n],i9100_ui_sounds_unavailable_width[n])){d[8]=1;goto failed;}
    }
    d[11]=2;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;allocated=TRUE;d[5]=address;
    if(address<4096 || address>0x3ffff000U)goto failed;
    for(unsigned n=0;n<9;n++){
        memcpy(code+n*64,i9100_ui_sounds_unavailable_prefix,36);code[n*64+14]=(uint8_t)(n+1);
        if(n==1 || n==2 || n==4)memcpy(code+n*64+36,i9100_ui_sounds_unavailable_mute,2);
        else memcpy(code+n*64+36,i9100_ui_sounds_unavailable_error,10);
    }
    if(copyout(code,address,sizeof(code))){d[8]=2;goto failed;}
    flush_dcache(address,sizeof(code),FALSE);invalidate_icache(address,sizeof(code),FALSE);
    result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;d[11]=3;
    for(unsigned n=0;n<9;n++){
        uint32_t page=sites[n]&~4095U;
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
        uint32_t branch[2]={0xf000f8dfU,address+n*64U+1U};touched|=1U<<n;
        if(copyout(branch,sites[n],8)){d[8]=3+n;goto failed;}
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
    }
    d[10]++;d[11]=4;d[30]=0xfffffa24U;
    OSMemoryBarrier();d[1]=2;i9100_ui_sounds_unavailable_guard=0;return 0;
failed:
    for(unsigned n=0;n<9;n++)if(touched&(1U<<n)){
        uint32_t page=sites[n]&~4095U;
        kern_return_t restore=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
        if(restore==KERN_SUCCESS)restore=copyout(original[n],sites[n],8);
        if(restore==KERN_SUCCESS)touched&=~(1U<<n);
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        kern_return_t protect=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
        if(restore!=KERN_SUCCESS || protect!=KERN_SUCCESS){d[9]=restore?restore:protect;error=EIO;}
    }
    if(allocated && !touched)vm_map_remove(map,address,address+4096,0);
    OSMemoryBarrier();d[1]=3;i9100_ui_sounds_unavailable_guard=0;return error;
}

void i9100_ui_sounds_unavailable_note(proc_t p,unsigned pc,unsigned phase)
{
    volatile uint32_t *d=i9100_ui_sounds_unavailable_diag;
    if(!p || d[1]!=2 || phase<1 || phase>9 || strcmp(p->p_comm,"SpringBoard") ||
       d[2]!=(uint32_t)p->p_pid || d[3]!=(uint32_t)(uintptr_t)current_task() ||
       d[12]!=(uint32_t)p->p_idversion || pc!=d[5]+(phase-1)*64U+22U ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return;
    if(!OSCompareAndSwap(0,1,&i9100_ui_sounds_unavailable_guard))return;
    unsigned counter=phase<=3?15U+phase:16U+phase;
    if(d[counter]!=UINT32_MAX)d[counter]++;
    if(d[19]!=UINT32_MAX)d[19]++;
    OSMemoryBarrier();i9100_ui_sounds_unavailable_guard=0;
}
#endif
