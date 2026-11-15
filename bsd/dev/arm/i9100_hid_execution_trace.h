#ifndef I9100_HID_EXECUTION_TRACE_H
#define I9100_HID_EXECUTION_TRACE_H
#include <vm/vm_map.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>
#include <mach/task.h>

/* Optional own-task probes: original callback/digitizer/GS arguments and
 * event bytes only. getpid is real; its temporary result/flags/registers are
 * restored by the thunk before replaying the original eight-byte prologue. */
/* v2 retains the v1 identity/count fields. 20..23 are the four upstream
 * counts; 24 is an actual one-shot getpid self-test, 25..29 expose scalar
 * SVC admission, 30 is its original return PC, 31 the verified cache slide. */
volatile uint32_t i9100_hid_execution_diag[32]={2};
volatile uint32_t i9100_hid_execution_rows[4+32*64];
volatile uint32_t i9100_hid_fixed_cadence_diag[16]={1};
extern volatile uint32_t i9100_touch_sender_identity[4];
static volatile UInt32 i9100_hid_execution_guard;
static uint32_t i9100_hid_execution_pending[4];
static uint32_t i9100_hid_execution_resume_pending;
static const uint8_t i9100_hid_execution_uuid[16]={
    0xe1,0xdc,0x74,0x43,0x4e,0x0e,0x39,0x38,0xb1,0x7f,0xf0,0xf7,0xad,0x85,0xd1,0x38};
static const uint32_t i9100_hid_execution_sites[7]={
    0x6094,0xcf08,0xb818,0x3024930c,0x31d23b74,0x31d244d8,0x31d2329c};
static const uint8_t i9100_hid_execution_width[7]={8,8,8,12,8,8,8};
static const uint8_t i9100_hid_execution_original[7][12]={
    {0x80,0xb5,0x6f,0x46,0x18,0x46,0x11,0x46},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0xf0,0xb5,0x03,0xaf,0x81,0xb0,0x04,0x46},
    /* dispatchHIDEvent's LDR.W spans bytes6..9: replay twelve complete
     * bytes, never the partial eight-byte instruction prefix. */
    {0x90,0xb5,0x01,0xaf,0x81,0xb0,0xd0,0xf8,0x3c,0x90,0x0b,0x46},
    {0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0x8d},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
    {0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d}};
static const uint8_t i9100_hid_execution_cache_uuid[2][16]={
    {0x66,0xc5,0x55,0x87,0x2b,0x2d,0x33,0x35,0x99,0x3b,0xea,0xa6,0x04,0x3f,0x5a,0xa3},
    {0xa9,0x8b,0xa9,0xfe,0xfc,0x73,0x33,0xe4,0xa5,0xa9,0x16,0x91,0x98,0x84,0x8c,0x62}};
static const uint8_t i9100_hid_execution_prefix[36]={
    0x2d,0xe9,0xff,0x5f,0xef,0xf3,0x00,0x8c,0x82,0xb0,0xcd,0xf8,0x00,0xc0,
    0x01,0x20,0x4f,0xf0,0x14,0x0c,0x80,0xdf,0xdd,0xf8,0x00,0xc0,0x02,0xb0,
    0x8c,0xf3,0x00,0x8c,0xbd,0xe8,0xff,0x5f};

/* iOS6 can set ContainsReportInterval again when a client registers a
 * request, even after the fixed-cadence kernel provider removed the property.
 * Restore that one capability bit before the original filter executes. Its
 * native no-rate-control branch delivers the original retained event; no
 * callback, packet, client deadline or filter return value is fabricated. */
static void i9100_hid_fixed_cadence(proc_t p,unsigned service,const uint8_t *event,unsigned bytes)
{
    int enabled=0;uint32_t type,options,transducer,sender[2],identity[4],fields[20];
    if(!PE_parse_boot_argn("i9100hidcadence",&enabled,sizeof(enabled)) || enabled!=1 ||
       !p || p->task!=current_task() || current_map()!=get_task_map(p->task) ||
       strcmp(p->p_comm,"backboardd") || memcmp(p->p_uuid,i9100_hid_execution_uuid,16) ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0 || bytes!=136)return;
    memcpy(&type,event+36,4);memcpy(&options,event+48,4);memcpy(sender,event+56,8);memcpy(&transducer,event+80,4);
    if(type!=11 || transducer!=35 || (options&0x80011U)!=0x80011U ||
       service<4096 || service>0x3fffffb0U || (service&3U))return;
    memcpy(identity,(const void*)i9100_touch_sender_identity,16);
    if(identity[0]!=1 || identity[1]!=1 || (!identity[2]&&!identity[3]) ||
       sender[0]!=identity[2] || sender[1]!=identity[3])return;
    volatile uint32_t *d=i9100_hid_fixed_cadence_diag;
    d[1]=1;d[7]=p->p_pid;d[8]=(uint32_t)(uintptr_t)p->task;d[9]=p->p_idversion;
    d[10]=service;d[12]=sender[0];d[13]=sender[1];if(d[2]!=UINT32_MAX)d[2]++;
    unsigned error=copyin(service,fields,sizeof(fields));d[15]=error;
    if(error){if(d[5]!=UINT32_MAX)d[5]++;return;}
    uint32_t isa;memcpy(&isa,event,4);
    /* Both are CF objects supplied by this exact native call. Refuse an
     * invalid runtime prefix, a missing registry handle or an unknown bit. */
    if(!isa || fields[0]!=isa || !(fields[1]&0x80U) || !fields[3] || fields[19]>1){
        if(d[6]!=UINT32_MAX)d[6]++;return;
    }
    d[11]=fields[19];
    if(!fields[19]){if(d[4]!=UINT32_MAX)d[4]++;return;}
    if(i9100_touch_sender_identity[1]!=1 || i9100_touch_sender_identity[2]!=sender[0] ||
       i9100_touch_sender_identity[3]!=sender[1] || p->task!=current_task() ||
       current_map()!=get_task_map(p->task) || memcmp(p->p_uuid,i9100_hid_execution_uuid,16))return;
    uint32_t zero=0;error=copyout(&zero,service+0x4cU,4);d[15]=error;
    if(error){if(d[5]!=UINT32_MAX)d[5]++;return;}
    if(d[3]!=UINT32_MAX)d[3]++;OSMemoryBarrier();
}

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

static int i9100_hid_execution_image_matches(uint32_t header,const uint8_t *uuid)
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

int i9100_hid_execution_bootstrap(proc_t p,unsigned return_pc,unsigned return_cpsr)
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
    vm_map_t map=current_map();vm_map_offset_t address=4096,sites[7];uint32_t mh[7],dyld[21];
    uint8_t original[7][12],code[512]={0};unsigned touched=0;boolean_t allocated=FALSE;int error=0;
    if(memcmp(p->p_uuid,i9100_hid_execution_uuid,16) || header<4096 || (header&4095U) ||
       header>0x40000000U-0x60000U || copyin(header,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[3]!=2)goto failed;
    task_dyld_info_data_t info={0};mach_msg_type_number_t info_count=TASK_DYLD_INFO_COUNT;
    if(return_pc<4096 || return_pc>0x3fffffffU || (return_cpsr&31U)!=16 ||
       task_info(current_task(),TASK_DYLD_INFO,(task_info_t)&info,&info_count)!=KERN_SUCCESS ||
       info_count!=TASK_DYLD_INFO_COUNT || info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_32 ||
       info.all_image_info_addr<4096 || info.all_image_info_addr>0x3fffffacU ||
       info.all_image_info_size<84 || copyin(info.all_image_info_addr,dyld,84) ||
       dyld[0]<12 || dyld[0]>15 || (dyld[20]&4095U) || dyld[20]>0x02800000U){d[8]=6;goto failed;}
    uint32_t slide=dyld[20];d[31]=slide;
    if(!i9100_hid_execution_image_matches(0x30242000U+slide,i9100_hid_execution_cache_uuid[0]) ||
       !i9100_hid_execution_image_matches(0x31d0e000U+slide,i9100_hid_execution_cache_uuid[1])){d[8]=7;goto failed;}
    for(unsigned n=0;n<7;n++){
        sites[n]=n<3?header+i9100_hid_execution_sites[n]-4096U:i9100_hid_execution_sites[n]+slide;
        if(copyin(sites[n],original[n],i9100_hid_execution_width[n]) ||
           memcmp(original[n],i9100_hid_execution_original[n],i9100_hid_execution_width[n])){d[8]=1;goto failed;}
    }
    d[11]=2;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;allocated=TRUE;d[5]=address;
    if(address<4096 || address>0x3ffff000U)goto failed;
    for(unsigned n=0;n<8;n++){
        memcpy(code+n*64,i9100_hid_execution_prefix,36);code[n*64+14]=(uint8_t)(n+1);
        for(unsigned k=36;k<48;k+=2){code[n*64+k]=0;code[n*64+k+1]=0xbf;}
        if(n<7)memcpy(code+n*64+36,original[n],i9100_hid_execution_width[n]);
        uint32_t destination=n<7?sites[n]+i9100_hid_execution_width[n]+1U:
            (return_pc&~1U)|((return_cpsr&(1U<<5))?1U:0U);
        uint32_t jump[2]={0xf000f8dfU,destination};memcpy(code+n*64+48,jump,8);
    }
    if(copyout(code,address,sizeof(code))){d[8]=2;goto failed;}
    flush_dcache(address,sizeof(code),FALSE);invalidate_icache(address,sizeof(code),FALSE);
    result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;d[11]=3;
    for(unsigned n=0;n<7;n++){
        uint32_t page=sites[n]&~4095U;
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
        uint32_t branch[2]={0xf000f8dfU,address+n*64U+1U};touched|=1U<<n;
        if(copyout(branch,sites[n],8)){d[8]=3+n;goto failed;}
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
    }
    d[10]++;d[11]=4;d[30]=return_pc;i9100_hid_execution_resume_pending=1;
    OSMemoryBarrier();d[1]=2;i9100_hid_execution_guard=0;return 0;
failed:
    for(unsigned n=0;n<7;n++)if(touched&(1U<<n)){
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

/* Called after normal bsdthread_register return values have been assigned.
 * The one extra real getpid preserves them and resumes that exact PC/state;
 * an absent self-test therefore cannot be mistaken for zero HID execution. */
void i9100_hid_execution_resume(proc_t p,unsigned *pc,unsigned *cpsr)
{
    volatile uint32_t *d=i9100_hid_execution_diag;
    if(!i9100_hid_execution_resume_pending || !p || d[1]!=2 ||
       d[2]!=(uint32_t)p->p_pid || d[3]!=(uint32_t)(uintptr_t)current_task() ||
       d[12]!=(uint32_t)p->p_idversion || d[30]!=*pc || (*cpsr&31U)!=16)return;
    i9100_hid_execution_resume_pending=0;*pc=d[5]+7U*64U;*cpsr|=1U<<5;
}

void i9100_hid_execution_note(proc_t p,unsigned pc,unsigned sp,unsigned phase)
{
    volatile uint32_t *d=i9100_hid_execution_diag;
    if(!p || d[1]!=2 || d[2]!=(uint32_t)p->p_pid ||
       d[3]!=(uint32_t)(uintptr_t)current_task() || d[12]!=(uint32_t)p->p_idversion ||
       strcmp(p->p_comm,"backboardd"))return;
    unsigned mask=0;
    if(phase<1 || phase>8)mask|=1U;
    else if(pc!=d[5]+(phase-1)*64U+22U)mask|=2U;
    if(sp<4096 || sp>0x3fffffc0U || (sp&3U))mask|=4U;
    if(ml_at_interrupt_context())mask|=8U;
    if(!ml_get_interrupts_enabled())mask|=16U;
    if(get_preemption_level()!=0)mask|=32U;
    if(OSCompareAndSwap(0,1,&i9100_hid_execution_guard)){
        if(d[25]!=UINT32_MAX)d[25]++;
        if(!(mask&3U) && d[26]!=UINT32_MAX)d[26]++;
        d[27]=mask;d[28]=pc;d[29]=phase;OSMemoryBarrier();i9100_hid_execution_guard=0;
    }
    if(mask)return;
    uint32_t saved[16]={0};uint8_t bytes[136]={0};unsigned stack_error=copyin(sp,saved,64);
    /* Five-argument callbacks pass sender/service in r2 and event in r3.
     * C++ dispatch receives (this,event,options); report filter gets r2. */
    unsigned pointer=(phase==1 || phase==5)?saved[5]:
        (phase==4 || phase==7)?saved[3]:phase==6?saved[4]:phase==8?0:saved[2];
    unsigned data_error=phase==8?0:UINT32_MAX,length=0;
    if(!stack_error && pointer>=4096 && pointer<=0x3fffff00U){
        if(phase==3){length=116;data_error=copyin(pointer,bytes,length);}
        else if(phase==7){length=32;data_error=copyin(pointer,bytes,length);}
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
    unsigned counter=phase<=3?15U+phase:16U+phase;
    if(d[counter]!=UINT32_MAX)d[counter]++;
    i9100_hid_execution_rows[1]=2;i9100_hid_execution_rows[2]=32;i9100_hid_execution_rows[3]=64;
    OSMemoryBarrier();r[0]=seq;i9100_hid_execution_rows[0]=seq;d[19]=seq;
    i9100_hid_execution_guard=0;
    if(phase==6 && !stack_error && !data_error)
        i9100_hid_fixed_cadence(p,saved[3],bytes,length);
}
#endif
