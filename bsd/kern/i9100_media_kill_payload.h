#ifndef I9100_MEDIA_KILL_PAYLOAD_H
#define I9100_MEDIA_KILL_PAYLOAD_H
/* Only the witnessed CoreMedia self-kill, in the faulting process's ordinary
 * syscall context. Preserve raw CFString storage and its native frame chain;
 * do not infer a string layout or change the signal/exception policy. */
volatile uint32_t i9100_media_kill_payload[64+128];
volatile uint32_t i9100_springboard_kill_payload[64+128];
extern volatile uint32_t i9100_audio_property_runtime[4];
extern volatile uint32_t i9100_audio_property_lock_diag[12];
volatile uint32_t i9100_media_audio_runtime[8+16];
#include "../../pexpert/arm/i9100_media_threads.h"
static volatile UInt32 i9100_media_kill_guard;
static volatile UInt32 i9100_springboard_kill_guard;
static void i9100_media_kill_note(proc_t p,const arm_saved_state_t *state,int signum,unsigned flavor)
{
    int enabled=0;
    if(!p || !state || signum!=SIGKILL || flavor ||
       (strcmp(p->p_comm,"mediaserverd") && strcmp(p->p_comm,"SpringBoard")) || (state->cpsr&0x1fU)!=0x10U ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0 ||
       !PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1)return;
    uint32_t slide=(state->lr&~1U)-0x316a426eU;
    unsigned springboard=!strcmp(p->p_comm,"SpringBoard");
    volatile UInt32 *guard=springboard?&i9100_springboard_kill_guard:&i9100_media_kill_guard;
    if((slide&4095U) || slide>=0x40000000U-0x392cc840U ||
       state->pc!=0x392cc838U+slide || state->r[1]!=SIGKILL ||
       !OSCompareAndSwap(0U,1U,guard))return;
    volatile uint32_t *out=springboard?i9100_springboard_kill_payload:i9100_media_kill_payload;
    if(!springboard)i9100_media_threads_note((unsigned)p->p_pid,slide);
    /* Preserve the first actual callback stage before this owner exits.
     * Installation alone cannot prove the new code ever executed. */
    if(!springboard && i9100_audio_property_runtime[0]==1 &&
       i9100_audio_property_runtime[1]==(uint32_t)p->p_pid &&
       i9100_audio_property_runtime[2]==(uint32_t)(uintptr_t)p->task){
        volatile uint32_t *audio=i9100_media_audio_runtime;
        audio[1]=p->p_pid;audio[2]=(uint32_t)(uintptr_t)p->task;
        unsigned address=i9100_audio_property_runtime[3];audio[3]=address;
        if(address>=4096U && address<=0x3fffffc0U)
            audio[4]=copyin((user_addr_t)address,(void *)&audio[8],64);
        else audio[4]=UINT32_MAX;
        unsigned site=i9100_audio_property_lock_diag[4];audio[5]=site;
        uint32_t branch[2]={0};
        if(site>=4096U && site<=0x3ffffff8U)
            audio[6]=copyin((user_addr_t)site,branch,8);
        else audio[6]=UINT32_MAX;
        if(!audio[6])audio[7]=branch[1];
        OSMemoryBarrier();audio[0]=1;
    }
    out[1]=p->p_pid;out[2]=(uint32_t)(uintptr_t)p->task;out[3]=state->pc;
    out[4]=state->lr;out[5]=state->sp;out[6]=state->r[7];out[12]=slide;out[13]=1;
    out[14]=signum;out[15]=springboard?2U:1U;
    uint32_t frame=state->r[7];
    for(unsigned n=0;n<16;n++){
        uint32_t pair[2];
        if((frame&3U) || frame<state->sp || frame>0x3ffffff8U || frame-state->sp>131072U){out[8]=1;break;}
        if(copyin((user_addr_t)frame,pair,8)){out[8]=2;break;}
        out[16+n*3]=frame;out[17+n*3]=pair[0];out[18+n*3]=pair[1];out[7]=n+1;
        if(pair[0]<=frame)break;frame=pair[0];
    }
    uint32_t address=state->r[5];out[9]=address;
    if(address>=4096 && address<0x40000000U){
        unsigned count=4096-(address&4095U);if(count>512)count=512;
        out[10]=copyin((user_addr_t)address,(void *)&out[64],count);
        if(!out[10])out[11]=count;
    }else out[10]=3;
    OSMemoryBarrier();out[0]=1;
}
#endif
