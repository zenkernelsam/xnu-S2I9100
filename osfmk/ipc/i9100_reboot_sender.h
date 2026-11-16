#ifndef I9100_REBOOT_SENDER_H
#define I9100_REBOOT_SENDER_H
#include <arm/thread.h>
extern void *find_user_regs(thread_t);
/* Sender-context observation before port-copyin/IPC locking. Read only the
 * current user's stack, cap at four requests/eight frame links, and preserve
 * the real reboot message/result. Rare lines survive an open host capture. */
volatile uint32_t i9100_reboot_sender_diag[4+4*48]={1,0,4,48};
static void i9100_reboot_sender_note(ipc_kmsg_t kmsg)
{
    int enabled=0;
    if(!kmsg || !kmsg->ikm_header)return;
    mach_msg_header_t *h=kmsg->ikm_header;
    vm_offset_t begin=(vm_offset_t)(kmsg+1),at=(vm_offset_t)h;
    if(at<begin || at-begin>kmsg->ikm_size || kmsg->ikm_size-(at-begin)<40)return;
    if(h->msgh_id!=401 || h->msgh_size!=40 || (h->msgh_bits&MACH_MSGH_BITS_COMPLEX))return;
    const uint8_t *body=(const uint8_t *)(h+1);
    static const uint8_t ndr[8]={0,0,0,0,1,0,0,0};
    if(memcmp(body,ndr,8))return;
    if(!PE_parse_boot_argn("i9100xpcdiag",&enabled,4) || enabled!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0)return;
    unsigned pid=proc_selfpid();char name[16]={0};proc_name(pid,name,sizeof(name));
    if(strcmp(name,"backboardd"))return;
    arm_saved_state_t *s=find_user_regs(current_thread());
    if(!s || (s->cpsr&31U)!=16 || s->sp<4096 || s->sp>=0x40000000U)return;
    UInt32 slot=i9100_reboot_sender_diag[1];
    if(slot>=4 || !OSCompareAndSwap(slot,slot+1,&i9100_reboot_sender_diag[1]))return;
    volatile uint32_t *r=&i9100_reboot_sender_diag[4+slot*48];
    memset((void*)r,0,48*4);r[1]=pid;r[2]=(uint32_t)(uintptr_t)current_task();
    r[3]=s->pc;r[4]=s->lr;r[5]=s->sp;r[6]=s->r[7];r[7]=s->cpsr;
    memcpy((void*)&r[8],body+8,8);unsigned fp=s->r[7],count=0;
    printf("I9REBOOT-S pid=%u pc=%08x lr=%08x sp=%08x fp=%08x flags=%08x:%08x\n",r[1],r[3],r[4],r[5],fp,r[9],r[8]);
    while(count<8 && fp>=s->sp && fp<=s->sp+65536U && fp<=0x3ffffff8U && !(fp&3U)){
        uint32_t link[2]={0};unsigned error=copyin(fp,link,8);unsigned index=12+count*4;
        r[index]=fp;r[index+1]=link[0];r[index+2]=link[1];r[index+3]=error;
        printf("I9REBOOT-F n=%u fp=%08x next=%08x lr=%08x err=%u\n",count,fp,link[0],link[1],error);
        count++;if(error || link[0]<=fp)break;fp=link[0];
    }
    r[10]=count;OSMemoryBarrier();r[0]=slot+1;
}
#endif
