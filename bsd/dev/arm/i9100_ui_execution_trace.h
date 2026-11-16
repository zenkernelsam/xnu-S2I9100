#ifndef I9100_UI_EXECUTION_TRACE_H
#define I9100_UI_EXECUTION_TRACE_H
#include <vm/vm_map.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>
#include <mach/task.h>
extern kern_return_t vm_map_enter(vm_map_t,vm_map_offset_t *,vm_map_size_t,
    vm_map_offset_t,int,vm_object_t,vm_object_offset_t,boolean_t,
    vm_prot_t,vm_prot_t,vm_inherit_t);
/* Observe original Setup UIKit execution; never replace an event or result. */
enum { I9100_UI_SITES=10, I9100_UI_CAPACITY=64 };
volatile uint32_t i9100_ui_execution_diag[32]={2};
volatile uint32_t i9100_ui_execution_rows[4+I9100_UI_CAPACITY*96];
static volatile UInt32 i9100_ui_execution_guard;
static uint32_t i9100_ui_execution_pending[4];
static const uint8_t i9100_ui_execution_uuid[16]={
0x58,0x0d,0xfd,0x1e,0xdb,0xd5,0x3d,0x33,0x84,0x4b,0xca,0x66,0x35,0xc0,0xb3,0xcd};
static const uint8_t i9100_ui_execution_cache_uuid[16]={
0xad,0x8b,0x3a,0xd2,0x3f,0x41,0x31,0x87,0xa1,0x78,0x17,0x9d,0xb3,0x9c,0xfa,0x6b};
/* The final entry is an offset from the UUID-verified Setup Mach header. */
static const uint32_t i9100_ui_execution_sites[I9100_UI_SITES]={
0x32f3284c,0x32f439cc,0x32f44d58,0x32f453e4,
0x33074028,0x3307604c,0x33076ee4,0x330185c0,0x0000ef18,0x32f85e94};
static const uint8_t i9100_ui_execution_original[I9100_UI_SITES][8]={
{0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
{0x4d,0xf2,0x6c,0x51,0xc0,0xf2,0x8a,0x61},
{0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0x8d},
{0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
{0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
{0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
{0xf0,0xb5,0x03,0xaf,0x4d,0xf8,0x04,0x8d},
{0xf0,0xb5,0x49,0xf6,0xa6,0x26,0x04,0x46},
{0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d},
{0x10,0x46,0xc7,0xf7,0x4b,0xbe,0x00,0xbf}};
static const uint8_t i9100_ui_execution_prefix[36]={
0x2d,0xe9,0xff,0x5f,0xef,0xf3,0x00,0x8c,0x82,0xb0,0xcd,0xf8,0x00,0xc0,
0x01,0x20,0x4f,0xf0,0x14,0x0c,0x80,0xdf,0xdd,0xf8,0x00,0xc0,0x02,0xb0,
0x8c,0xf3,0x00,0x8c,0xbd,0xe8,0xff,0x5f};
static void i9100_ui_mov32(uint8_t *out,unsigned reg,uint32_t value)
{
    for(unsigned n=0;n<2;n++){
        unsigned v=(value>>(n*16))&65535U;
        uint16_t words[2]={(uint16_t)((n?0xf2c0U:0xf240U)|((v>>12)&15U)|(((v>>11)&1U)<<10)),
            (uint16_t)((((v>>8)&7U)<<12)|(reg<<8)|(v&255U))};
        memcpy(out+n*4,words,4);
    }
}
static void i9100_ui_retina_code(uint8_t *out,unsigned header,unsigned slide)
{
    /* Call the original UIImage API only for the original Next-image CFString.
     * No synthetic UIImage, control action, device scale or UI data is returned.
     * The original kitImageNamed: MOV/tail branch is relocated explicitly. */
    memset(out,0,80);memcpy(out,i9100_ui_execution_prefix,14);
    i9100_ui_mov32(out+14,12,header+0x62544U);
    const uint16_t guard[2]={0x4562U,0xd10cU};memcpy(out+22,guard,4);
    i9100_ui_mov32(out+26,1,0x333ae22cU+slide);
    i9100_ui_mov32(out+34,2,0x40000000U);
    i9100_ui_mov32(out+42,12,0x3310e9c5U+slide);
    const uint16_t call=0x47e0U;memcpy(out+50,&call,2);
    memcpy(out+52,i9100_ui_execution_prefix+22,14);
    const uint16_t original_mov=0x4610U;memcpy(out+66,&original_mov,2);
    const uint32_t tail[2]={0xf000f8dfU,0x32f4db31U+slide};memcpy(out+68,tail,8);
}
static void i9100_ui_execution_record(proc_t p,user_addr_t header)
{
    int enabled=0;
    if(!p || !p->task || strcmp(p->p_comm,"Setup") ||
       !PE_parse_boot_argn("i9100uitrace",&enabled,4) || enabled!=1 ||
       memcmp(p->p_uuid,i9100_ui_execution_uuid,16) ||
       header<4096 || header>0x3ff00000U || (header&4095U))return;
    if(!OSCompareAndSwap(0,1,&i9100_ui_execution_guard))return;
    i9100_ui_execution_pending[0]=p->p_pid;i9100_ui_execution_pending[1]=(uint32_t)(uintptr_t)p->task;
    i9100_ui_execution_pending[2]=header;i9100_ui_execution_pending[3]=p->p_idversion;
    OSMemoryBarrier();i9100_ui_execution_guard=0;
}
static int i9100_ui_execution_image_matches(unsigned header)
{
    uint32_t mh[7],lc[2];uint8_t uuid[16];
    if(header<4096 || header>0x3fffb000U || (header&4095U) || copyin(header,mh,28) ||
       mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[4]>128 || mh[5]<24 || mh[5]>0x4000)return 0;
    unsigned at=header+28,end=at+mh[5];
    for(unsigned n=0;n<mh[4];n++){
        if(at>end-8 || copyin(at,lc,8) || lc[1]<8 || lc[1]>end-at)return 0;
        if(lc[0]==27)return lc[1]==24 && !copyin(at+8,uuid,16) && !memcmp(uuid,i9100_ui_execution_cache_uuid,16);
        at+=lc[1];
    }return 0;
}
int i9100_ui_execution_bootstrap(proc_t p,unsigned return_pc,unsigned return_cpsr)
{
    int enabled=0,abi=0,retina=0;
    if(!p || !p->task || strcmp(p->p_comm,"Setup") ||
       !PE_parse_boot_argn("i9100uitrace",&enabled,4) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,4) || abi!=6 ||
       current_task()!=p->task || current_map()!=get_task_map(p->task) || get_task_numacts(p->task)!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return 0;
    if(!OSCompareAndSwap(0,1,&i9100_ui_execution_guard))return 0;
    volatile uint32_t *d=i9100_ui_execution_diag;
    if(d[6]>=8 || i9100_ui_execution_pending[0]!=(unsigned)p->p_pid ||
       i9100_ui_execution_pending[1]!=(unsigned)(uintptr_t)p->task ||
       i9100_ui_execution_pending[3]!=(unsigned)p->p_idversion){i9100_ui_execution_guard=0;return 0;}
    unsigned header=i9100_ui_execution_pending[2];memset(i9100_ui_execution_pending,0,16);
    d[1]=1;d[2]=p->p_pid;d[3]=(uint32_t)(uintptr_t)p->task;d[4]=header;d[6]++;d[12]=p->p_idversion;
    d[7]=d[8]=d[9]=0;d[11]=1;
    PE_parse_boot_argn("i9100uiretina",&retina,4);unsigned site_count=retina==1?10U:9U;d[26]=retina==1;
    vm_map_t map=current_map();vm_map_offset_t address=4096,sites[I9100_UI_SITES];
    uint32_t mh[7],dyld[21];uint8_t original[I9100_UI_SITES][8],code[9*64+80]={0};unsigned touched=0;
    boolean_t allocated=FALSE;int error=0;
    if(memcmp(p->p_uuid,i9100_ui_execution_uuid,16) || header<4096 || header>0x3ff00000U ||
       (header&4095U) || copyin(header,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[3]!=2)goto failed;
    task_dyld_info_data_t info={0};mach_msg_type_number_t info_count=TASK_DYLD_INFO_COUNT;
    if(return_pc<4096 || return_pc>=0x40000000U || (return_cpsr&31U)!=16 ||
       task_info(current_task(),TASK_DYLD_INFO,(task_info_t)&info,&info_count)!=KERN_SUCCESS ||
       info_count!=TASK_DYLD_INFO_COUNT || info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_32 ||
       info.all_image_info_addr<4096 || info.all_image_info_addr>0x3fffffacU || info.all_image_info_size<84 ||
       copyin(info.all_image_info_addr,dyld,84) || dyld[0]<12 || dyld[0]>15 ||
       (dyld[20]&4095U) || dyld[20]>0x0375b000U){d[8]=6;goto failed;}
    unsigned slide=dyld[20];d[31]=slide;
    if(!i9100_ui_execution_image_matches(0x32f2f000U+slide)){d[8]=7;goto failed;}
    if(retina==1){
        uint8_t api[8],selector[29];static const uint8_t expected[8]={0x10,0x46,0x79,0xf6,0x87,0xbf,0x00,0xbf};
        if(copyin(0x3310e9c4U+slide,api,8) || memcmp(api,expected,8) ||
           copyin(0x333ae22cU+slide,selector,29) || memcmp(selector,"setPreferredSharedImageScale:",29)){
            d[8]=8;goto failed;
        }
    }
    for(unsigned n=0;n<site_count;n++){
        sites[n]=i9100_ui_execution_sites[n]+(n==8?header:slide);
        if(copyin(sites[n],original[n],8) || memcmp(original[n],i9100_ui_execution_original[n],8)){d[8]=1;goto failed;}
    }d[11]=2;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;allocated=TRUE;d[5]=address;
    if(address<4096 || address>0x3ffff000U)goto failed;
    for(unsigned n=0;n<9;n++){
        memcpy(code+n*64,i9100_ui_execution_prefix,36);code[n*64+14]=n+1;
        memcpy(code+n*64+36,original[n],8);uint32_t jump[2]={0xf000f8dfU,sites[n]+9U};memcpy(code+n*64+44,jump,8);
    }
    if(retina==1)i9100_ui_retina_code(code+9*64,header,slide);
    if(copyout(code,address,sizeof(code))){d[8]=2;goto failed;}
    flush_dcache(address,sizeof(code),FALSE);invalidate_icache(address,sizeof(code),FALSE);
    result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;d[11]=3;
    for(unsigned n=0;n<site_count;n++){
        unsigned page=sites[n]&~4095U;
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
        uint32_t branch[2]={0xf000f8dfU,address+n*64+1U};touched|=1U<<n;
        if(copyout(branch,sites[n],8)){d[8]=3+n;goto failed;}
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
        if(result!=KERN_SUCCESS)goto failed;
    }
    d[10]++;d[11]=4;d[30]=sites[0];OSMemoryBarrier();d[1]=2;i9100_ui_execution_guard=0;return 0;
failed:
    for(unsigned n=0;n<site_count;n++)if(touched&(1U<<n)){
        unsigned page=sites[n]&~4095U;
        kern_return_t restore=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
        if(restore==KERN_SUCCESS)restore=copyout(original[n],sites[n],8);
        if(restore==KERN_SUCCESS)touched&=~(1U<<n);
        flush_dcache(sites[n],8,FALSE);invalidate_icache(sites[n],8,FALSE);
        kern_return_t protect=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
        if(restore!=KERN_SUCCESS || protect!=KERN_SUCCESS){d[9]=restore?restore:protect;error=EIO;}
    }
    if(allocated && !touched)vm_map_remove(map,address,address+4096,0);
    OSMemoryBarrier();d[1]=3;i9100_ui_execution_guard=0;return error;
}
void i9100_ui_execution_note(proc_t p,unsigned pc,unsigned sp,unsigned phase)
{
    volatile uint32_t *d=i9100_ui_execution_diag;
    if(!p || d[1]!=2 || phase<1 || phase>9 || strcmp(p->p_comm,"Setup") ||
       d[2]!=(unsigned)p->p_pid || d[3]!=(unsigned)(uintptr_t)current_task() || d[12]!=(unsigned)p->p_idversion ||
       p->task!=current_task() || current_map()!=get_task_map(p->task) || memcmp(p->p_uuid,i9100_ui_execution_uuid,16) ||
       pc!=d[5]+(phase-1)*64+22 || sp<4096 || sp>0x3fffffc0 || (sp&3) ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return;
    uint32_t saved[16]={0};uint8_t actor[192]={0},event[128]={0};unsigned se=copyin(sp,saved,64);
    unsigned actor_len=phase==1?116:phase==2?80:phase==8?55:192;
    unsigned event_len=(phase==2 || phase==8)?0:(phase==1 || (phase>=5 && phase<=7))?128:32;
    unsigned ae=UINT32_MAX,ee=event_len?UINT32_MAX:0;
    if(!se && saved[2]>=4096 && saved[2]<=0x40000000U-actor_len)ae=copyin(saved[2],actor,actor_len);
    if(!se && phase>=5 && phase<=7){
        /* Keep the event/index-path and selection state at the execution site.
         * This is an observation payload, not an in-place UIKit structure. */
        unsigned arg=phase==7?saved[4]:saved[5];ee=UINT32_MAX;
        if(arg>=4096 && arg<=0x3fffffe0U)ee=copyin(arg,event,32);
        if(!ee && saved[2]>=4096 && saved[2]<=0x40000000U-784U){
            ee=copyin(saved[2]+728,event+32,4);
            if(!ee)ee=copyin(saved[2]+756,event+36,28);
            if(!ee)ee=copyin(saved[2]+564,event+64,32);
            if(!ee && phase!=7 && saved[4]>=4096 && saved[4]<=0x3fffffe0U)
                ee=copyin(saved[4],event+96,32);
        }else if(!ee)ee=UINT32_MAX;
    }else if(!se && event_len && saved[4]>=4096 && saved[4]<=0x40000000U-event_len)
        ee=copyin(saved[4],event,event_len);
    if(!OSCompareAndSwap(0,1,&i9100_ui_execution_guard))return;
    if(d[1]!=2 || d[2]!=(unsigned)p->p_pid || d[12]!=(unsigned)p->p_idversion){i9100_ui_execution_guard=0;return;}
    unsigned seq=d[19]+1;if(!seq)seq=1;volatile uint32_t *r=&i9100_ui_execution_rows[4+((seq-1)&(I9100_UI_CAPACITY-1))*96];
    memset((void*)r,0,384);r[1]=p->p_pid;r[2]=p->p_idversion;r[3]=d[3];r[4]=phase;r[5]=pc;r[6]=sp;r[7]=se;
    memcpy((void*)&r[8],saved+2,16);r[12]=ae;r[13]=actor_len;r[14]=ee;r[15]=event_len;
    memcpy((void*)&r[16],actor,192);memcpy((void*)&r[64],event,128);
    unsigned counter=phase<=3?15+phase:16+phase;
    if(d[counter]!=UINT32_MAX)d[counter]++;
    i9100_ui_execution_rows[1]=2;i9100_ui_execution_rows[2]=I9100_UI_CAPACITY;i9100_ui_execution_rows[3]=96;
    OSMemoryBarrier();r[0]=seq;i9100_ui_execution_rows[0]=seq;d[19]=seq;i9100_ui_execution_guard=0;
}
#endif
