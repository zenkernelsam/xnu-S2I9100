#ifndef I9100_COMMCENTER_FAULT_H
#define I9100_COMMCENTER_FAULT_H
/* Original iOS6 CommCenterClassic assertion stores 1 at VA 1 at 0x1958b4.
 * r4 still points to the assertion expression; LR was clobbered by string
 * assignment. Retain the first expression and its own stack before teardown.
 * This observes the failure without bypassing it or claiming radio support. */
volatile uint32_t i9100_commcenter_fault_payload[16+128];
static volatile UInt32 i9100_commcenter_fault_guard;
static void i9100_commcenter_fault_note(const struct i9100_fault_snapshot *state,
    const uint32_t *registers)
{
    thread_t thread=current_thread();int enabled=0;
    if (!thread || !thread->task || !thread->task->bsd_info ||
        (state->psr&0x3fU)!=0x30U || state->type!=4U || state->far!=1U ||
        registers[0]!=1U || get_preemption_level()!=0 ||
        !ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
        !PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1 ||
        strncmp(proc_name_address(thread->task->bsd_info),"CommCenterClass",15))return;
    uint32_t slide=state->pc-0x1958b4U,address=registers[4];
    if ((slide&4095U) || slide>=0x40000000U-0x203000U ||
        address<4096U || address>=0x40000000U ||
        !OSCompareAndSwap(0U,1U,&i9100_commcenter_fault_guard))return;
    volatile uint32_t *out=i9100_commcenter_fault_payload;
    memset((void *)out,0,sizeof(i9100_commcenter_fault_payload));
    out[1]=proc_pid(thread->task->bsd_info);out[2]=(uint32_t)(uintptr_t)thread->task;
    out[3]=state->pc;out[4]=state->lr;out[5]=state->sp;out[6]=registers[7];
    out[7]=slide;out[8]=address;out[9]=state->far;out[13]=1U;
    /* Read one bounded byte at a time so an inaccessible page leaves the
     * actual prefix and error, rather than fabricated trailing bytes. */
    for(unsigned n=0;n<512U && address+n<0x40000000U;n++){
        unsigned char byte=0;
        int error=copyin((user_addr_t)(address+n),&byte,1);
        if(error){out[11]=(uint32_t)error;break;}
        ((volatile unsigned char *)&out[16])[n]=byte;out[10]=n+1U;
        if(!byte){out[12]=1U;break;}
    }
    OSMemoryBarrier();out[0]=1U;
}
#endif
