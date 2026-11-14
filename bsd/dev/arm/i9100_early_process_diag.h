#ifndef I9100_EARLY_PROCESS_DIAG_H
#define I9100_EARLY_PROCESS_DIAG_H
/* Retain the first 63 PID identities after they leave allproc. Names are
 * kernel-owned scalars of the current syscall's referenced proc. No user
 * reads, clocks, process actions or printf; unchanged identities are free. */
volatile uint32_t i9100_early_process_diag[4+64*12]={1,64,12,0};
static volatile UInt32 i9100_early_process_guard;
static void i9100_early_process_note(proc_t p,unsigned code)
{
    if(!p || p->p_pid<=0 || p->p_pid>=64)return;
    volatile uint32_t *r=&i9100_early_process_diag[4+(unsigned)p->p_pid*12];
    if(r[0] && r[2]==(unsigned)p->p_idversion && r[3]==(unsigned)(uintptr_t)p->task &&
       !memcmp((const void *)&r[4],p->p_comm,16))return;
    int enabled=0;
    if(ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0 ||
       !PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1 ||
       !OSCompareAndSwap(0U,1U,&i9100_early_process_guard))return;
    unsigned updates=r[9]+1;r[0]=0;r[1]=p->p_pid;r[2]=p->p_idversion;
    r[3]=(unsigned)(uintptr_t)p->task;memcpy((void *)&r[4],p->p_comm,16);
    r[8]=code;r[9]=updates;r[10]=1;r[11]=0;
    OSMemoryBarrier();r[0]=1;i9100_early_process_diag[3]++;
    OSMemoryBarrier();i9100_early_process_guard=0;
}
#endif
