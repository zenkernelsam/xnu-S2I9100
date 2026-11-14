#ifndef I9100_EARLY_EXIT_DIAG_H
#define I9100_EARLY_EXIT_DIAG_H
/* Original early-PID wait status and owned saved-register scalars. This
 * runs before teardown in the existing exit observer: no user reads, clock,
 * printf, reference acquisition, suspension or termination-policy changes. */
extern void *find_user_regs(thread_t);
volatile uint32_t i9100_early_exit_diag[4+64*16]={1,64,16,0};
static void i9100_early_exit_note(proc_t p,int status,thread_t self,struct uthread *ut)
{
    int enabled=0;
    if(!p || p->p_pid<=0 || p->p_pid>=64 ||
       !PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1)return;
    unsigned own=p->task && get_threadtask(self)==p->task;
    arm_saved_state_t *state=own?find_user_regs(self):NULL;
    volatile uint32_t *r=&i9100_early_exit_diag[4+(unsigned)p->p_pid*16];
    r[0]=0;r[1]=p->p_pid;r[2]=p->p_idversion;r[3]=(unsigned)(uintptr_t)p->task;
    r[4]=status;r[5]=own;r[6]=state && ((state->cpsr&31U)==16U);
    r[7]=r[8]=r[9]=0;
    if(r[6]){r[7]=state->pc;r[8]=state->lr;r[9]=state->cpsr;}
    r[10]=own && ut?ut->uu_exception:0;r[11]=own && ut?ut->uu_code:0;r[12]=own && ut?ut->uu_subcode:0;
    r[13]=(unsigned)(uintptr_t)get_threadtask(self);r[14]=p->p_csflags;r[15]=1;
    OSMemoryBarrier();r[0]=1;OSIncrementAtomic((volatile SInt32 *)&i9100_early_exit_diag[3]);
}
#endif
