#ifndef I9100_AUDIO_PROPERTY_LOCK_H
#define I9100_AUDIO_PROPERTY_LOCK_H
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
/* BSD's exported task header deliberately hides struct task. */
extern int get_task_numacts(task_t);
/* vm_map_enter is MACH_KERNEL_PRIVATE in the exported header; its object
 * offset is 64-bit even in this ARM32 build. An implicit call loses a word
 * and changes needs_copy/protection/inheritance. Keep the real prototype. */
extern kern_return_t vm_map_enter(vm_map_t,vm_map_offset_t *,vm_map_size_t,
    vm_map_offset_t,int,vm_object_t,vm_object_offset_t,boolean_t,
    vm_prot_t,vm_prot_t,vm_inherit_t);
/* Own-task runtime repair for the exact witnessed iOS6 CoreMedia callback.
 * Its external AudioObject query enters VirtualAudio while the global lock
 * is held, reversing the initialization thread's plugin->global order.
 * Private COW code releases the actual recursive depth (at most16) around
 * the query and reacquires the same number of successfully released holds.
 * No API result is forged and the cached/SD image stays unchanged. */
volatile uint32_t i9100_audio_property_lock_diag[12];
volatile uint32_t i9100_audio_property_lock_history[1+8*12];
volatile uint32_t i9100_audio_property_runtime[4];
static const unsigned char i9100_audio_property_original[24]={
    0xd5,0xf8,0xd0,0x00,0x06,0xac,0x10,0x23,0x03,0xa9,0x8d,0xe8,
    0x18,0x00,0x00,0x22,0x00,0x23,0xb0,0x47,0x08,0xbb,0x08,0x98};
static const unsigned char i9100_audio_property_code[256]={
    0x2d,0xe9,0x00,0x03,0x4f,0xb5,0xdf,0xf8,0xf4,0xc0,0xdc,0xf8,0x00,0x00,0x40,0x1c,
    0xcc,0xf8,0x00,0x00,0x01,0x20,0xcc,0xf8,0x04,0x00,0x5f,0xf0,0x01,0x08,0xe8,0x68,
    0xcc,0xf8,0x08,0x00,0xcc,0xf8,0x24,0x60,0xcc,0xf8,0x28,0x50,0x01,0x68,0xcc,0xf8,
    0x0c,0x10,0x45,0xf2,0x58,0x42,0xc4,0xf6,0x55,0x52,0x91,0x42,0x0c,0xd1,0x81,0x68,
    0xcc,0xf8,0x10,0x10,0x01,0xf0,0x0c,0x02,0x08,0x2a,0x05,0xd1,0x09,0x0c,0x01,0x29,
    0x02,0xd3,0x10,0x29,0x00,0xd8,0x88,0x46,0xdf,0xf8,0xa0,0xc0,0xcc,0xf8,0x14,0x80,
    0xc1,0x46,0x5f,0xf0,0x00,0x08,0xe8,0x68,0xdf,0xf8,0x84,0xc0,0xe0,0x47,0xdf,0xf8,
    0x8c,0xc0,0xcc,0xf8,0x18,0x00,0x00,0x28,0x04,0xd1,0x08,0xf1,0x01,0x08,0xb9,0xf1,
    0x01,0x09,0xf0,0xd1,0xdf,0xf8,0x74,0xc0,0xcc,0xf8,0x1c,0x80,0x02,0x20,0xcc,0xf8,
    0x04,0x00,0xbd,0xe8,0x4f,0x40,0x82,0xb0,0xd5,0xf8,0xd0,0x00,0x0a,0xac,0x10,0x23,
    0x07,0xa9,0x8d,0xe8,0x18,0x00,0x00,0x22,0x00,0x23,0xb0,0x47,0x4f,0xb5,0xdf,0xf8,
    0x4c,0xc0,0xcc,0xf8,0x20,0x00,0x03,0x20,0xcc,0xf8,0x04,0x00,0xb8,0xf1,0x00,0x0f,
    0x08,0xd0,0xe8,0x68,0xdf,0xf8,0x2c,0xc0,0xe0,0x47,0x00,0x28,0x02,0xd1,0xb8,0xf1,
    0x01,0x08,0xf6,0xd1,0xdf,0xf8,0x24,0xc0,0x04,0x20,0xcc,0xf8,0x04,0x00,0xbd,0xe8,
    0x4f,0x40,0x02,0xb0,0xbd,0xe8,0x00,0x03,0xdf,0xf8,0x0c,0xc0,0x60,0x47,0x00,0xbf,
    0xc1,0x7e,0x21,0x39,0xa1,0x5f,0x21,0x39,0x5d,0xe8,0x69,0x31,0x00,0x00,0x00,0x00};
static int i9100_audio_property_range(uint32_t slide)
{
    return !(slide&4095U) && slide<0x40000000U-0x394951c0U;
}
static kern_return_t i9100_audio_property_rx(vm_map_t map,vm_map_offset_t start)
{
    kern_return_t result=vm_map_protect(map,start,start+4096,
        VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
    if(result==KERN_SUCCESS)result=vm_map_protect(map,start,start+4096,
        VM_PROT_READ|VM_PROT_EXECUTE,TRUE);
    return result;
}
static void i9100_audio_property_lock_prepare(proc_t p,unsigned code,
    const arm_saved_state_t *state)
{
    int abi=0,enabled=0,requested=0;
    if(code!=372U || !p || strcmp(p->p_comm,"mediaserverd") ||
       !p->task || get_task_numacts((task_t)p->task)!=1 ||
       (state->cpsr&0x1fU)!=0x10U || ml_at_interrupt_context() ||
       !ml_get_interrupts_enabled() || get_preemption_level()!=0 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,sizeof(abi)) || abi!=6 ||
       !PE_parse_boot_argn("i9100audiolock",&requested,sizeof(requested)) || requested!=1 ||
       !PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1)return;
    uint32_t slide=(state->pc&~1U)-0x392cdc34U;
    if(!i9100_audio_property_range(slide))return;
    volatile uint32_t *diag=i9100_audio_property_lock_diag;
    uint32_t status=diag[0],attempt=diag[11];
    if(status==1 || attempt>=8 || (status && diag[1]==(uint32_t)p->p_pid &&
       diag[2]==(uint32_t)(uintptr_t)p->task))return;
    unsigned char original[24];
    uint32_t site=0x3169e848U+slide;
    if(copyin((user_addr_t)site,original,sizeof(original)) ||
       memcmp(original,i9100_audio_property_original,sizeof(original)))return;
    if(!OSCompareAndSwap(status,1U,&diag[0]))return;
    for(unsigned n=1;n<12;n++)diag[n]=0;
    diag[11]=attempt+1;
    /* ANYWHERE uses this as a lower bound; avoid the private vm_map_min
     * macro that is absent from BSD's exported vm_map.h. Keep page zero. */
    vm_map_t map=current_map();vm_map_offset_t address=4096;
    diag[1]=p->p_pid;diag[2]=(uint32_t)(uintptr_t)p->task;diag[3]=slide;diag[4]=site;
    uint32_t storage[64];unsigned char *bytes=(unsigned char *)storage;
    memcpy(bytes,i9100_audio_property_code,sizeof(storage));
    uint32_t *literals=(uint32_t *)&bytes[240];
    literals[0]+=slide;literals[1]+=slide;literals[2]+=slide;
    /* Mutable page is data only; generated code page remains maximum RX. */
    diag[10]=1;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,
        VM_OBJECT_NULL,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);
    diag[6]=(uint32_t)result;
    if(result!=KERN_SUCCESS || address<4096 || address>0x3ffff000U)goto failed;
    diag[5]=(uint32_t)address;
    vm_map_offset_t runtime=4096;
    result=vm_map_enter(map,&runtime,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_COPY);
    diag[6]=(uint32_t)result;if(result!=KERN_SUCCESS || runtime<4096 || runtime>0x3ffff000U)goto failed;
    uint32_t data[16]={0};if(copyout(data,(user_addr_t)runtime,sizeof(data))){diag[7]=4;goto failed;}
    literals[3]=(uint32_t)runtime;
    i9100_audio_property_runtime[0]=1;i9100_audio_property_runtime[1]=p->p_pid;
    i9100_audio_property_runtime[2]=(uint32_t)(uintptr_t)p->task;
    i9100_audio_property_runtime[3]=(uint32_t)runtime;
    diag[10]=2;
    if(copyout(bytes,(user_addr_t)address,sizeof(storage))){diag[7]=1;goto failed;}
    diag[10]=3;
    flush_dcache((vm_offset_t)address,sizeof(storage),FALSE);
    invalidate_icache((vm_offset_t)address,sizeof(storage),FALSE);
    diag[10]=4;
    result=i9100_audio_property_rx(map,address);
    diag[6]=(uint32_t)result;if(result!=KERN_SUCCESS)goto failed;
    diag[10]=5;
    result=vm_map_protect(map,site&~4095U,(site&~4095U)+4096,
        VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
    diag[6]=(uint32_t)result;if(result!=KERN_SUCCESS)goto failed;
    diag[10]=6;
    /* The process has one thread. Install only after the target is present;
     * failed writes attempt restoration before making this page executable. */
    uint32_t branch[2]={0xf000f8dfU,(uint32_t)address|1U};
    if(copyout(branch,(user_addr_t)site,sizeof(branch))){
        diag[7]=2;
        if(copyout(original,(user_addr_t)site,8))diag[7]=3;
        flush_dcache((vm_offset_t)site,8,FALSE);
        invalidate_icache((vm_offset_t)site,8,FALSE);
        goto restore;
    }
    flush_dcache((vm_offset_t)site,8,FALSE);
    invalidate_icache((vm_offset_t)site,8,FALSE);
    diag[8]=1;
    diag[10]=7;
restore:
    result=i9100_audio_property_rx(map,site&~4095U);diag[9]=(uint32_t)result;
    diag[10]=8;
    OSMemoryBarrier();diag[0]=diag[8] && result==KERN_SUCCESS?2U:3U;
    goto record;
failed:
    OSMemoryBarrier();diag[0]=3U;
record:
    for(unsigned n=0;n<12;n++)i9100_audio_property_lock_history[1+attempt*12+n]=diag[n];
    OSMemoryBarrier();i9100_audio_property_lock_history[0]=attempt+1;
}
#endif
