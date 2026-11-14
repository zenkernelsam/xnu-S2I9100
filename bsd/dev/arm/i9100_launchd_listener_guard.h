#ifndef I9100_LAUNCHD_LISTENER_GUARD_H
#define I9100_LAUNCHD_LISTENER_GUARD_H
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>
extern int get_task_numacts(task_t);
extern kern_return_t vm_map_enter(vm_map_t,vm_map_offset_t *,vm_map_size_t,
    vm_map_offset_t,int,vm_object_t,vm_object_offset_t,boolean_t,
    vm_prot_t,vm_prot_t,vm_inherit_t);

/* schema,state,pid,task,header,site,trampoline,vm-result,copy-error,
 * installed,restore-result,stage,uuid-match,cleanup-result,reserved,idversion */
volatile uint32_t i9100_launchd_listener_diag[16]={1};
static const unsigned char i9100_launchd_listener_uuid[16]={
    0x10,0xff,0x8d,0x45,0x04,0x02,0x39,0xd1,0xb4,0x79,0xcd,0xdf,0x29,0xe6,0xb5,0x0e};
static const unsigned char i9100_launchd_listener_original[16]={
    0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d,0x82,0xb0,0x06,0x46,0x8a,0x46,0x74,0x6e};
static const unsigned char i9100_launchd_listener_caller[4]={0x08,0xf0,0xab,0xfd};
/* Only NULL changes behavior: BOOTSTRAP_NO_MEMORY, matching launchd 842.92.1.
 * A valid job executes the original prologue and resumes at entry+8. */
static const unsigned char i9100_launchd_listener_code[24]={
    0x28,0xb1,0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d,0xdf,0xf8,
    0x08,0xf0,0x40,0xf2,0x51,0x40,0x70,0x47,0x00,0x00,0x00,0x00};

static kern_return_t i9100_launchd_listener_rx(vm_map_t map,vm_map_offset_t start)
{
    kern_return_t result=vm_map_protect(map,start,start+4096,
        VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
    if(result==KERN_SUCCESS)result=vm_map_protect(map,start,start+4096,
        VM_PROT_READ|VM_PROT_EXECUTE,TRUE);
    return result;
}

static void i9100_launchd_listener_prepare(proc_t p,user_addr_t image_header)
{
    int enabled=0,abi=0;
    if(!p || p->p_pid!=1 || strcmp(p->p_comm,"launchd") ||
       !PE_parse_boot_argn("i9100launchdguard",&enabled,sizeof(enabled)) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,sizeof(abi)) || abi!=6)return;
    volatile uint32_t *diag=i9100_launchd_listener_diag;
    if(!OSCompareAndSwap(0,1,&diag[1]))return;
    diag[2]=p->p_pid;diag[3]=(uint32_t)(uintptr_t)p->task;
    diag[4]=(uint32_t)image_header;diag[15]=p->p_idversion;diag[11]=1;
    vm_map_t map=VM_MAP_NULL;vm_map_offset_t address=4096,site=0;
    boolean_t allocated=FALSE,touched=FALSE;
    uint32_t code[6],mh[7];unsigned char original[16],caller[4];
    if(!p->task || current_task()!=(task_t)p->task ||
       current_map()!=get_task_map((task_t)p->task) || get_task_numacts((task_t)p->task)!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)goto failed;
    map=current_map();diag[11]=2;
    if(image_header<4096 || image_header>0x40000000ULL-0x26000 || (image_header&4095U) ||
       memcmp(p->p_uuid,i9100_launchd_listener_uuid,16))goto failed;
    diag[12]=1;
    if(copyin(image_header,mh,sizeof(mh)) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[3]!=2){diag[8]=1;goto failed;}
    /* load_result.mach_header is the real executable header after ASLR.
     * Never derive an executable slide from the shared-cache syscall PC. */
    site=(vm_map_offset_t)image_header+(0xc1a0U-0x1000U);diag[5]=site;
    if(copyin((user_addr_t)site,original,sizeof(original)) ||
       memcmp(original,i9100_launchd_listener_original,sizeof(original)) ||
       copyin(image_header+(0x3646U-0x1000U),caller,sizeof(caller)) ||
       memcmp(caller,i9100_launchd_listener_caller,sizeof(caller))){diag[8]=2;goto failed;}
    diag[11]=3;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,
        VM_OBJECT_NULL,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);
    diag[7]=result;if(result!=KERN_SUCCESS)goto failed;
    allocated=TRUE;diag[6]=(uint32_t)address;
    if(address<4096 || address>0x3ffff000U)goto failed;
    memcpy(code,i9100_launchd_listener_code,sizeof(code));code[5]=(uint32_t)(site+8)|1U;
    diag[11]=4;
    if(copyout(code,(user_addr_t)address,sizeof(code))){diag[8]=3;goto failed;}
    flush_dcache((vm_offset_t)address,sizeof(code),FALSE);
    invalidate_icache((vm_offset_t)address,sizeof(code),FALSE);
    diag[11]=5;result=i9100_launchd_listener_rx(map,address);diag[7]=result;
    if(result!=KERN_SUCCESS)goto failed;
    diag[11]=6;result=vm_map_protect(map,site&~4095U,(site&~4095U)+4096,
        VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);diag[7]=result;
    if(result!=KERN_SUCCESS)goto failed;
    uint32_t branch[2]={0xf000f8dfU,(uint32_t)address|1U};
    touched=TRUE;diag[11]=7;
    if(copyout(branch,(user_addr_t)site,sizeof(branch))){diag[8]=4;goto failed;}
    diag[9]=1;flush_dcache((vm_offset_t)site,8,FALSE);invalidate_icache((vm_offset_t)site,8,FALSE);
    diag[11]=8;result=i9100_launchd_listener_rx(map,site&~4095U);diag[7]=result;
    if(result!=KERN_SUCCESS)goto failed;
    OSMemoryBarrier();diag[1]=2;return;
failed:
    if(touched){
        kern_return_t restore=vm_map_protect(map,site&~4095U,(site&~4095U)+4096,
            VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
        if(restore==KERN_SUCCESS){
            restore=copyout(original,(user_addr_t)site,8);
            if(!restore){touched=FALSE;diag[9]=0;}
            flush_dcache((vm_offset_t)site,8,FALSE);invalidate_icache((vm_offset_t)site,8,FALSE);
        }
        kern_return_t protect=i9100_launchd_listener_rx(map,site&~4095U);
        diag[10]=restore!=KERN_SUCCESS?restore:protect;
    }
    if(allocated&&!touched)diag[13]=vm_map_remove(map,address,address+4096,0);
    OSMemoryBarrier();diag[1]=3;
}
#endif
