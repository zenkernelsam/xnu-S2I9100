#ifndef I9100_HID_EXECUTION_TRACE_H
#define I9100_HID_EXECUTION_TRACE_H
#include <vm/vm_map.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>

/* Optional own-task probes: original callback/digitizer/GS arguments and
 * event bytes only. getpid is real; its temporary result/flags/registers are
 * restored by the thunk before replaying the original eight-byte prologue. */
volatile uint32_t i9100_hid_execution_diag[24]={1};
volatile uint32_t i9100_hid_execution_rows[4+32*64];
static volatile UInt32 i9100_hid_execution_guard;
static uint32_t i9100_hid_execution_pending[4];
static const uint8_t i9100_hid_execution_uuid[16]={
    0xe1,0xdc,0x74,0x43,0x4e,0x0e,0x39,0x38,0xb1,0x7f,0xf0,0xf7,0xad,0x85,0xd1,0x38};
static const uint32_t i9100_hid_execution_sites[3]={0x6094,0xcf08,0xb818};
static const uint8_t i9100_hid_execution_original[3][8]={
    {0x80,0xb5,0x6f,0x46,0x18,0x46,0x11,0x46},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0xf0,0xb5,0x03,0xaf,0x81,0xb0,0x04,0x46}};
static const uint8_t i9100_hid_execution_prefix[36]={
    0x2d,0xe9,0xff,0x5f,0xef,0xf3,0x00,0x8c,0x82,0xb0,0xcd,0xf8,0x00,0xc0,
    0x01,0x20,0x4f,0xf0,0x14,0x0c,0x80,0xdf,0xdd,0xf8,0x00,0xc0,0x02,0xb0,
    0x8c,0xf3,0x00,0x8c,0xbd,0xe8,0xff,0x5f};

static void i9100_hid_execution_record(proc_t p,user_addr_t header)
{
    int enabled=0;
    if(!p || !p->task || strcmp(p->p_comm,"backboardd") ||
       !PE_parse_boot_argn("i9100hidtrace",&enabled,sizeof(enabled)) || enabled!=1 ||
       memcmp(p->p_uuid,i9100_hid_execution_uuid,16) ||
       header<4096 || header>0x40000000U-0x60000U || (header&4095U))return;
    if(!OSCompareAndSwap(0,1,&i9100_hid_execution_guard))return;
    i9100_hid_execution_pending[0]=p->p_pid;
    i9100_hid_execution_pending[1]=(uint32_t)(uintptr_t)p->task;
    i9100_hid_execution_pending[2]=(uint32_t)header;
    i9100_hid_execution_pending[3]=p->p_idversion;
    OSMemoryBarrier();i9100_hid_execution_guard=0;
}

int i9100_hid_execution_bootstrap(proc_t p)
{
    int enabled=0,abi=0;
    if(!p || strcmp(p->p_comm,"backboardd") ||
       !PE_parse_boot_argn("i9100hidtrace",&enabled,sizeof(enabled)) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,sizeof(abi)) || abi!=6 ||
       !p->task || current_task()!=(task_t)p->task ||
       current_map()!=get_task_map((task_t)p->task) || get_task_numacts((task_t)p->task)!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return 0;
    if(!OSCompareAndSwap(0,1,&i9100_hid_execution_guard))return 0;
    volatile uint32_t *d=i9100_hid_execution_diag;
    if(d[6]>=8 || i9100_hid_execution_pending[0]!=(uint32_t)p->p_pid ||
       i9100_hid_execution_pending[1]!=(uint32_t)(uintptr_t)p->task ||
       i9100_hid_execution_pending[3]!=(uint32_t)p->p_idversion){i9100_hid_execution_guard=0;return 0;}
    uint32_t header=i9100_hid_execution_pending[2];memset(i9100_hid_execution_pending,0,16);
    d[6]++;d[1]=1;d[2]=p->p_pid;d[3]=(uint32_t)(uintptr_t)p->task;d[4]=header;d[12]=p->p_idversion;
    d[7]=d[8]=d[9]=0;d[11]=1;
    vm_map_t map=current_map();vm_map_offset_t address=4096,sites[3];uint32_t mh[7];
    uint8_t original[3][8],code[192]={0};unsigned touched=0;boolean_t allocated=FALSE;int error=0;
    if(memcmp(p->p_uuid,i9100_hid_execution_uuid,16) || header<4096 || (header&4095U) ||
       header>0x40000000U-0x60000U || copyin(header,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[3]!=2)goto failed;
    for(unsigned n=0;n<3;n++){
        sites[n]=header+i9100_hid_execution_sites[n]-4096U;
        if(copyin(sites[n],original[n],8) || memcmp(original[n],i9100_hid_execution_original[n],8)){d[8]=1;goto failed;}
    }
    d[11]=2;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;allocated=TRUE;d[5]=address;
    if(address<4096 || address>0x3ffff000U)goto failed;
    for(unsigned n=0;n<3;n++){
        memcpy(code+n*64,i9100_hid_execution_prefix,36);code[n*64+14]=(uint8_t)(n+1);
        memcpy(code+n*64+36,original[n],8);
        uint32_t jump[2]={0xf000f8dfU,sites[n]+8U+1U};memcpy(code+n*64+44,jump,8);
    }
    if(copyout(code,address,sizeof(code))){d[8]=2;goto failed;}
    flush_dcache(address,sizeof(code),FALSE);invalidate_icache(address,sizeof(code),FALSE);
    result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;d[11]=3;
    for(unsigned n=0;n<3;n++){
        uint32_t page=sites[n]&~4095U;
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
        uint32_t branch[2]={0xf000f8dfU,address+n*64U+1U};touched|=1U<<n;
        if(copyout(branch,sites[n],8)){d[8]=3+n;goto failed;}
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
    }
    d[10]++;d[11]=4;OSMemoryBarrier();d[1]=2;i9100_hid_execution_guard=0;return 0;
failed:
    for(unsigned n=0;n<3;n++)if(touched&(1U<<n)){
        uint32_t page=sites[n]&~4095U;
        kern_return_t restore=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
        if(restore==KERN_SUCCESS)restore=copyout(original[n],sites[n],8);
        if(restore==KERN_SUCCESS)touched&=~(1U<<n);
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        kern_return_t protect=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
        if(restore!=KERN_SUCCESS || protect!=KERN_SUCCESS){d[9]=restore?restore:protect;error=EIO;}
    }
    if(allocated && !touched)vm_map_remove(map,address,address+4096,0);
    OSMemoryBarrier();d[1]=3;i9100_hid_execution_guard=0;return error;
}

void i9100_hid_execution_note(proc_t p,unsigned pc,unsigned sp,unsigned phase)
{
    volatile uint32_t *d=i9100_hid_execution_diag;
    if(!p || phase<1 || phase>3 || d[1]!=2 || d[2]!=(uint32_t)p->p_pid ||
       d[3]!=(uint32_t)(uintptr_t)current_task() || d[12]!=(uint32_t)p->p_idversion ||
       strcmp(p->p_comm,"backboardd") || pc!=d[5]+(phase-1)*64U+22U ||
       sp<4096 || sp>0x3fffffc0U || (sp&3U) || ml_at_interrupt_context() ||
       !ml_get_interrupts_enabled() || get_preemption_level()!=0)return;
    uint32_t saved[16]={0};uint8_t bytes[136]={0};unsigned stack_error=copyin(sp,saved,64);
    unsigned pointer=phase==1?saved[4]:saved[2],data_error=UINT32_MAX,length=0;
    if(!stack_error && pointer>=4096 && pointer<=0x3fffff00U){
        if(phase==3){length=116;data_error=copyin(pointer,bytes,length);}
        else{
            data_error=copyin(pointer,bytes,40);
            if(!data_error){
                uint32_t size;memcpy(&size,bytes+32,4);
                if(size>=8 && size<=104){length=32+size;data_error=copyin(pointer,bytes,length);}
                else {length=40;data_error=EINVAL;}
            }
        }
    }
    if(d[1]!=2 || d[2]!=(uint32_t)p->p_pid || d[3]!=(uint32_t)(uintptr_t)current_task() ||
       d[12]!=(uint32_t)p->p_idversion)return;
    if(!OSCompareAndSwap(0,1,&i9100_hid_execution_guard))return;
    unsigned seq=d[19]+1U;if(!seq)seq=1;
    volatile uint32_t *r=&i9100_hid_execution_rows[4+((seq-1)&31U)*64];
    memset((void *)r,0,256);r[1]=p->p_pid;r[2]=p->p_idversion;r[3]=d[3];r[4]=phase;r[5]=pc;r[6]=sp;r[7]=stack_error;
    memcpy((void *)&r[8],saved+2,16);r[12]=pointer;r[13]=data_error;r[14]=length;r[15]=saved[0];
    memcpy((void *)&r[16],bytes,136);memcpy((void *)&r[50],saved+2,56);
    if(d[15+phase]!=UINT32_MAX)d[15+phase]++;
    i9100_hid_execution_rows[1]=1;i9100_hid_execution_rows[2]=32;i9100_hid_execution_rows[3]=64;
    OSMemoryBarrier();r[0]=seq;i9100_hid_execution_rows[0]=seq;d[19]=seq;
    i9100_hid_execution_guard=0;
}
#endif
