#ifndef I9100_QUIET_CHARGE_FEEDBACK_H
#define I9100_QUIET_CHARGE_FEEDBACK_H
#include <vm/vm_map.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>

/* Explicit temporary UI policy, not an audio-server/RPC replacement.
 * This exact void method only plays charging feedback sound 1106.
 * Battery telemetry, charger control and other sounds remain original. */
volatile uint32_t i9100_quiet_charge_diag[16]={1};
static volatile UInt32 i9100_quiet_charge_guard;
static uint32_t i9100_quiet_charge_pending[4];
static const unsigned char i9100_quiet_charge_uuid[16]={
    0xbd,0x1d,0x77,0xd1,0x54,0xef,0x3d,0xff,0xbb,0x53,0xd8,0x93,0x37,0xc8,0xa0,0x8f};
static const unsigned char i9100_quiet_charge_original[8]={
    0x40,0xf2,0x52,0x40,0x9e,0xf1,0xe2,0xbe};
static const unsigned char i9100_quiet_charge_caller[20]={
    0x40,0xb9,0x46,0xf2,0x0a,0x60,0xc0,0xf2,0x23,0x00,
    0x78,0x44,0x01,0x68,0x50,0x46,0xfa,0xf1,0x8c,0xef};
static const unsigned char i9100_quiet_charge_return[8]={
    0x70,0x47,0x00,0xbf,0x00,0xbf,0x00,0xbf};

static void i9100_quiet_charge_record(proc_t p,user_addr_t header)
{
    int enabled=0;
    if(!p || !p->task || strcmp(p->p_comm,"SpringBoard") ||
       !PE_parse_boot_argn("i9100quietcharge",&enabled,sizeof(enabled)) || enabled!=1 ||
       memcmp(p->p_uuid,i9100_quiet_charge_uuid,16) ||
       header<4096 || header>0x40000000U-0x2a4000U || (header&4095U))return;
    /* posix_spawn can load the child while executing in its parent's task.
     * Record only kernel scalars here; patch later in the child's own
     * single-threaded libSystem bootstrap. */
    if(!OSCompareAndSwap(0,1,&i9100_quiet_charge_guard))return;
    i9100_quiet_charge_pending[0]=p->p_pid;
    i9100_quiet_charge_pending[1]=(uint32_t)(uintptr_t)p->task;
    i9100_quiet_charge_pending[2]=(uint32_t)header;
    i9100_quiet_charge_pending[3]=p->p_idversion;
    OSMemoryBarrier();i9100_quiet_charge_guard=0;
}

int i9100_quiet_charge_bootstrap(proc_t p)
{
    int enabled=0,abi=0;
    if(!p || strcmp(p->p_comm,"SpringBoard") ||
       !PE_parse_boot_argn("i9100quietcharge",&enabled,sizeof(enabled)) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,sizeof(abi)) || abi!=6)return 0;
    if(!p->task || current_task()!=(task_t)p->task ||
       current_map()!=get_task_map((task_t)p->task) || get_task_numacts((task_t)p->task)!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return 0;
    if(!OSCompareAndSwap(0,1,&i9100_quiet_charge_guard))return 0;
    if(i9100_quiet_charge_pending[0]!=(uint32_t)p->p_pid ||
       i9100_quiet_charge_pending[1]!=(uint32_t)(uintptr_t)p->task ||
       i9100_quiet_charge_pending[3]!=(uint32_t)p->p_idversion){i9100_quiet_charge_guard=0;return 0;}
    user_addr_t header=i9100_quiet_charge_pending[2];
    memset(i9100_quiet_charge_pending,0,sizeof(i9100_quiet_charge_pending));
    volatile uint32_t *d=i9100_quiet_charge_diag;
    if(d[6]>=8U){i9100_quiet_charge_guard=0;return 0;}
    d[6]++;d[1]=1;d[2]=p->p_pid;d[3]=(uint32_t)(uintptr_t)p->task;d[4]=(uint32_t)header;
    d[7]=d[8]=d[9]=d[10]=d[11]=0;d[13]=1;d[14]=p->p_idversion;
    vm_map_t map=current_map();vm_map_offset_t site=0,page=0;boolean_t touched=FALSE;
    unsigned char original[8],caller[20];uint32_t mh[7];int error=0;
    if(header<4096 || header>0x40000000U-0x2a4000U || (header&4095U) ||
       memcmp(p->p_uuid,i9100_quiet_charge_uuid,16))goto failed;
    d[10]=1;
    if(copyin(header,mh,sizeof(mh)) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[3]!=2){d[8]=1;goto failed;}
    site=(vm_map_offset_t)header+0x10958U;page=site&~4095U;d[5]=site;
    if(copyin((user_addr_t)site,original,sizeof(original)) ||
       memcmp(original,i9100_quiet_charge_original,8) ||
       copyin(header+0x17ef8U,caller,sizeof(caller)) ||
       memcmp(caller,i9100_quiet_charge_caller,20)){d[8]=2;goto failed;}
    d[11]=1;d[13]=2;
    kern_return_t result=vm_map_protect(map,page,page+4096,
        VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;
    touched=TRUE;d[13]=3;
    if(copyout(i9100_quiet_charge_return,(user_addr_t)site,8)){d[8]=3;goto failed;}
    flush_dcache((vm_offset_t)site,8,FALSE);invalidate_icache((vm_offset_t)site,8,FALSE);
    d[13]=4;result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;
    d[12]++;d[13]=5;OSMemoryBarrier();d[1]=2;i9100_quiet_charge_guard=0;return 0;
failed:
    if(touched){
        kern_return_t restore=vm_map_protect(map,page,page+4096,
            VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
        if(restore==KERN_SUCCESS)restore=copyout(original,(user_addr_t)site,8);
        flush_dcache((vm_offset_t)site,8,FALSE);invalidate_icache((vm_offset_t)site,8,FALSE);
        kern_return_t protect=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
        d[9]=restore!=KERN_SUCCESS?restore:protect;
        /* Never enter user code after a failed restoration/protection. */
        if(d[9])error=EIO;
    }
    OSMemoryBarrier();d[1]=3;i9100_quiet_charge_guard=0;return error;
}
#endif
