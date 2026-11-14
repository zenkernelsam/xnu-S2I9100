/* First installed media task's real pthread waits in ordinary syscall context.
 * Read only own map using bounded copyin before original blocking operations.
 * Capture eight distinct mutex waits, including the observed external callback
 * stage; never alter locks, return values or thread/signal policy. */
#ifndef I9100_AUDIO_WAIT_DIAG_H
#define I9100_AUDIO_WAIT_DIAG_H
volatile uint32_t i9100_audio_wait_diag[1+8*100];
static volatile UInt32 i9100_audio_wait_guard;
static void i9100_audio_wait_note(proc_t p,unsigned code,unsigned argument_error,
    const arm_saved_state_t *state)
{
    if(argument_error || (code!=301U && code!=302U) || !p || !state ||
       strcmp(p->p_comm,"mediaserverd") || (state->cpsr&0x1fU)!=0x10U ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0 ||
       i9100_audio_property_runtime[0]!=1 ||
       i9100_audio_property_lock_history[0]<1 ||
       i9100_audio_property_lock_history[2]!=(uint32_t)p->p_pid ||
       i9100_audio_property_lock_history[3]!=(uint32_t)(uintptr_t)p->task ||
       i9100_audio_property_runtime[1]!=(uint32_t)p->p_pid ||
       i9100_audio_property_runtime[2]!=(uint32_t)(uintptr_t)p->task)return;
    int enabled=0;
    if(!PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1)return;
    unsigned address=i9100_audio_property_runtime[3];uint32_t runtime[16];
    if(address<4096 || address>0x3fffffc0U ||
       copyin((user_addr_t)address,runtime,sizeof(runtime)) || runtime[1]!=2U ||
       !OSCompareAndSwap(0U,1U,&i9100_audio_wait_guard))return;
    unsigned count=i9100_audio_wait_diag[0];
    for(unsigned n=0;n<count && n<8;n++){
        volatile uint32_t *old=&i9100_audio_wait_diag[1+n*100];
        if(old[0] && old[1]==(uint32_t)p->p_pid &&
           old[3]==(uint32_t)(uintptr_t)current_thread() && old[4]==code &&
           old[5]==state->r[0]){i9100_audio_wait_guard=0;return;}
    }
    if(count>=8){i9100_audio_wait_guard=0;return;}
    /* Reserve only under the nonblocking guard; no user memory while held. */
    volatile uint32_t *out=&i9100_audio_wait_diag[1+count*100];
    memset((void *)out,0,100*4);i9100_audio_wait_diag[0]=count+1;
    out[1]=p->p_pid;out[2]=(uint32_t)(uintptr_t)p->task;
    out[3]=(uint32_t)(uintptr_t)current_thread();out[4]=code;out[5]=state->r[0];
    out[6]=state->pc;out[7]=state->lr;out[8]=state->sp;out[9]=state->r[7];
    out[12]=1;out[13]=1;
    memcpy((void *)&out[14],state->r,13*4);out[27]=state->cpsr;
    memcpy((void *)&out[44],runtime,sizeof(runtime));
    OSMemoryBarrier();i9100_audio_wait_guard=0;
    if(state->r[0]>=4096U && state->r[0]<=0x3fffffc0U){
        out[12]=copyin((user_addr_t)state->r[0],(void *)&out[28],64);
    }
    uint32_t frame=state->r[7];
    for(unsigned n=0;n<8;n++){
        uint32_t pair[2];
        if((frame&3U) || frame<state->sp || frame>0x3ffffff8U || frame-state->sp>131072U){out[11]=1;break;}
        if(copyin((user_addr_t)frame,pair,8)){out[11]=2;break;}
        out[60+n*3]=frame;out[61+n*3]=pair[0];out[62+n*3]=pair[1];out[10]=n+1;
        if(pair[0]<=frame)break;frame=pair[0];
    }
    OSMemoryBarrier();out[0]=count+1;
}
#endif
