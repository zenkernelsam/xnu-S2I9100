#ifndef I9100_UI_DENSITY_H
#define I9100_UI_DENSITY_H
#include <vm/vm_map.h>
#include <machine/machine_routines.h>
#include <libkern/OSAtomic.h>
#include <mach/task.h>

/* Optional S2 display calibration for the three original iOS6 UI processes.
 * The original MG screen factory still measures the framebuffer and computes
 * its screen class. Only its main-screen-scale CFNumber becomes Float32.
 * Install before that factory has run, in this process's private COW map.
 * Other apps are deliberately outside this first hardware qualification. */
volatile uint32_t i9100_ui_density_diag[3][20];
static uint32_t i9100_ui_density_pending[3][4];
static volatile UInt32 i9100_ui_density_guard;
static const char *i9100_ui_density_names[3]={"backboardd","SpringBoard","Setup"};
static const uint8_t i9100_ui_density_uuids[3][16]={
 {0xe1,0xdc,0x74,0x43,0x4e,0x0e,0x39,0x38,0xb1,0x7f,0xf0,0xf7,0xad,0x85,0xd1,0x38},
 {0xbd,0x1d,0x77,0xd1,0x54,0xef,0x3d,0xff,0xbb,0x53,0xd8,0x93,0x37,0xc8,0xa0,0x8f},
 {0x58,0x0d,0xfd,0x1e,0xdb,0xd5,0x3d,0x33,0x84,0x4b,0xca,0x66,0x35,0xc0,0xb3,0xcd}};
static const uint8_t i9100_ui_density_mg_uuid[16]={
 0xd0,0xdc,0xe7,0x2a,0x60,0x4b,0x37,0x30,0x9b,0x87,0x14,0xfb,0x35,0xd7,0x44,0x68};
static const uint8_t i9100_ui_density_original[8]={0x00,0x20,0x09,0x21,0xcd,0xf8,0x20,0xb0};

static int i9100_ui_density_slot(proc_t p)
{
    if(p)for(unsigned n=0;n<3;n++)if(!strcmp(p->p_comm,i9100_ui_density_names[n]) &&
        !memcmp(p->p_uuid,i9100_ui_density_uuids[n],16))return (int)n;
    return -1;
}
static unsigned i9100_ui_density_option(void)
{
    int value=0,abi=0,native=0;
    PE_parse_boot_argn("i9100uidensity",&value,4);
    PE_parse_boot_argn("i9100hidabi",&abi,4);
    PE_parse_boot_argn("i9100setupnative",&native,4);
    return abi==6 && native==1 && (value==1333 || value==1500)?(unsigned)value:0;
}
static void i9100_ui_density_record(proc_t p,user_addr_t header)
{
    int n=i9100_ui_density_slot(p);unsigned option=i9100_ui_density_option();
    if(n<0 || !option || !p->task || header<4096 || header>0x3ff00000U || (header&4095U))return;
    if(!OSCompareAndSwap(0,1,&i9100_ui_density_guard))return;
    uint32_t *pending=i9100_ui_density_pending[n];
    pending[0]=p->p_pid;pending[1]=(uint32_t)(uintptr_t)p->task;pending[2]=header;pending[3]=p->p_idversion;
    OSMemoryBarrier();i9100_ui_density_guard=0;
}
static int i9100_ui_density_image(unsigned header)
{
    uint32_t mh[7],lc[2];uint8_t uuid[16];
    if(copyin(header,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 || mh[4]>128 || mh[5]<24 || mh[5]>0x4000)return 0;
    unsigned at=header+28,end=at+mh[5];
    for(unsigned n=0;n<mh[4];n++){
        if(at>end-8 || copyin(at,lc,8) || lc[1]<8 || lc[1]>end-at)return 0;
        if(lc[0]==27)return lc[1]==24 && !copyin(at+8,uuid,16) && !memcmp(uuid,i9100_ui_density_mg_uuid,16);
        at+=lc[1];
    }return 0;
}
static void i9100_ui_density_mov32(uint8_t *out,unsigned reg,uint32_t value)
{
    for(unsigned n=0;n<2;n++){
        unsigned v=(value>>(n*16))&65535U;
        uint16_t words[2]={(uint16_t)((n?0xf2c0U:0xf240U)|((v>>12)&15U)|(((v>>11)&1U)<<10)),
            (uint16_t)((((v>>8)&7U)<<12)|(reg<<8)|(v&255U))};
        memcpy(out+n*4,words,4);
    }
}
static void i9100_ui_density_code(uint8_t *out,unsigned slide,unsigned option)
{
    /* r2 already addresses the original stack slot. Preserve the original
     * integer r11 for screen classification. r12 is ABI scratch; no VFP use.
     * Both original/replacement MOVS pairs leave N=0/Z=0 and preserve C/V. */
    static const uint8_t prefix[4]={0x00,0x20,0x0c,0x21};
    static const uint8_t store[4]={0xcd,0xf8,0x20,0xc0};
    memcpy(out,prefix,4);
    i9100_ui_density_mov32(out+4,12,option==1333?0x3faaaaabU:0x3fc00000U);
    memcpy(out+12,store,4);
    uint32_t tail[2]={0xf000f8dfU,0x3867f55dU+slide};memcpy(out+16,tail,8);
}
int i9100_ui_density_bootstrap(proc_t p,unsigned pc,unsigned cpsr)
{
    int slot=i9100_ui_density_slot(p);unsigned option=i9100_ui_density_option();
    if(slot<0 || !option || !p->task || current_task()!=p->task || current_map()!=get_task_map(p->task) ||
       get_task_numacts(p->task)!=1 || ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return 0;
    if(!OSCompareAndSwap(0,1,&i9100_ui_density_guard))return 0;
    uint32_t *pending=i9100_ui_density_pending[slot];volatile uint32_t *d=i9100_ui_density_diag[slot];
    if(pending[0]!=(unsigned)p->p_pid || pending[1]!=(unsigned)(uintptr_t)p->task || pending[3]!=(unsigned)p->p_idversion){i9100_ui_density_guard=0;return 0;}
    unsigned header=pending[2];memset(pending,0,16);
    d[0]=1;d[1]=1;d[2]=p->p_pid;d[3]=(unsigned)(uintptr_t)p->task;d[4]=header;d[6]++;
    d[7]=d[8]=d[9]=d[11]=0;d[12]=p->p_idversion;d[13]=option;
    vm_map_t map=current_map();vm_map_offset_t address=4096;unsigned site=0,page=0;
    uint32_t dyld[21],cache[2];uint8_t original[8],code[24];int allocated=0,touched=0,error=0;
    task_dyld_info_data_t info={0};mach_msg_type_number_t count=TASK_DYLD_INFO_COUNT;
    if(pc<4096 || pc>=0x40000000U || (cpsr&31U)!=16 ||
       task_info(current_task(),TASK_DYLD_INFO,(task_info_t)&info,&count)!=KERN_SUCCESS || count!=TASK_DYLD_INFO_COUNT ||
       info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_32 || info.all_image_info_addr<4096 ||
       info.all_image_info_addr>0x3fffffacU || info.all_image_info_size<84 || copyin(info.all_image_info_addr,dyld,84) ||
       dyld[0]<12 || dyld[0]>15 || (dyld[20]&4095U) || dyld[20]>0x0375b000U){d[8]=1;goto failed;}
    unsigned slide=dyld[20];d[14]=slide;
    if(!i9100_ui_density_image(0x38679000U+slide)){d[8]=2;goto failed;}
    if(copyin(0x3acd7d4cU+slide,cache,8)){d[8]=3;goto failed;}
    d[16]=cache[0];d[17]=cache[1];
    if(cache[0] || cache[1]){d[8]=4;goto failed;} /* Never change already-published screen metrics. */
    site=0x3867f554U+slide;page=site&~4095U;d[15]=site;
    if(copyin(site,original,8) || memcmp(original,i9100_ui_density_original,8)){d[8]=5;goto failed;}
    d[11]=1;
    kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;allocated=1;d[5]=address;
    if(address<4096 || address>0x3ffff000U){d[8]=6;goto failed;}
    i9100_ui_density_code(code,slide,option);
    if(copyout(code,address,24)){d[8]=7;goto failed;}
    flush_dcache(address,24,FALSE);invalidate_icache(address,24,FALSE);
    result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;d[11]=2;
    result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;
    uint32_t branch[2]={0xf000f8dfU,address+1U};touched=1;
    if(copyout(branch,site,8)){d[8]=8;goto failed;}
    flush_dcache(site,8,FALSE);invalidate_icache(site,8,FALSE);
    result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
    if(result!=KERN_SUCCESS)goto failed;
    d[10]++;d[11]=3;OSMemoryBarrier();d[1]=2;i9100_ui_density_guard=0;return 0;
failed:
    if(touched){
        kern_return_t restore=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
        if(restore==KERN_SUCCESS)restore=copyout(original,site,8);
        if(restore==KERN_SUCCESS)touched=0;
        flush_dcache(site,8,FALSE);invalidate_icache(site,8,FALSE);
        kern_return_t protect=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
        if(restore!=KERN_SUCCESS || protect!=KERN_SUCCESS){d[9]=restore?restore:protect;error=EIO;}
    }
    if(allocated && !touched)vm_map_remove(map,address,address+4096,0);
    OSMemoryBarrier();d[1]=3;i9100_ui_density_guard=0;return error;
}
#endif
