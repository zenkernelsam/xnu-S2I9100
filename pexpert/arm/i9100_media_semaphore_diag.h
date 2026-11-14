#ifndef I9100_MEDIA_SEMAPHORE_DIAG_H
#define I9100_MEDIA_SEMAPHORE_DIAG_H
/* One exact CMSessionCreate wait, matched through its original own-task
 * frame chain. Semaphore hooks only copy caller-provided kernel scalars;
 * no user copy, clock, printf, VM/port walk or scheduling under a lock. */
extern volatile uint32_t i9100_audio_property_lock_history[1+8*12];
extern unsigned int real_ncpus;
volatile uint32_t i9100_media_semaphore_diag[12+128*16];
volatile uint32_t i9100_media_dispatch_wait[16+64];
static volatile UInt32 i9100_media_semaphore_claim, i9100_media_semaphore_guard;
static unsigned i9100_media_semaphore_target;

static void i9100_media_semaphore_record(unsigned stage,unsigned semaphore,
    unsigned count,unsigned count_known,unsigned result,unsigned flags,unsigned auxiliary)
{
    if(!i9100_media_semaphore_target || semaphore!=i9100_media_semaphore_target)return;
    unsigned finish=(stage==5U || stage==6U || stage==8U);
    if(!OSCompareAndSwap(0U,1U,&i9100_media_semaphore_guard)){
        OSIncrementAtomic((volatile SInt32 *)&i9100_media_semaphore_diag[9]);
        if(finish){i9100_media_semaphore_diag[8]=stage;i9100_media_semaphore_target=0;}
        return;
    }
    unsigned n=i9100_media_semaphore_diag[0];
    if(n<128U){
        thread_t thread=current_thread();uint64_t tid=thread_tid(thread);
        volatile uint32_t *r=&i9100_media_semaphore_diag[12+n*16];
        r[1]=stage;r[2]=i9100_media_semaphore_diag[4];r[3]=(unsigned)(uintptr_t)thread;
        r[4]=(uint32_t)tid;r[5]=(uint32_t)(tid>>32);r[6]=(unsigned)(uintptr_t)current_task();
        r[7]=semaphore;r[8]=i9100_media_semaphore_diag[7];r[9]=count;r[10]=count_known;
        r[11]=result;r[12]=flags;r[13]=auxiliary;r[14]=(unsigned)(uintptr_t)__builtin_return_address(0);r[15]=1;
        OSMemoryBarrier();r[0]=n+1;i9100_media_semaphore_diag[0]=n+1;
    }else OSIncrementAtomic((volatile SInt32 *)&i9100_media_semaphore_diag[9]);
    OSMemoryBarrier();i9100_media_semaphore_guard=0;
    if(finish){i9100_media_semaphore_diag[8]=stage;i9100_media_semaphore_target=0;}
}

static void i9100_media_semaphore_begin(unsigned semaphore,unsigned name)
{
    task_t task=current_task();
    if(i9100_media_semaphore_claim || !semaphore || !name || !task || task==kernel_task || real_ncpus!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0 ||
       i9100_audio_property_lock_history[0]<1 ||
       i9100_audio_property_lock_history[3]!=(unsigned)(uintptr_t)task ||
       i9100_audio_property_lock_history[2]!=(unsigned)proc_selfpid())return;
    arm_saved_state_t *s=current_thread()->machine.uss;
    unsigned slide=i9100_audio_property_lock_history[4];
    if(!s || (s->cpsr&31U)!=16U || slide>=0x40000000U-0x39494dd4U ||
       s->pc!=0x392bcf04U+slide)return;
    unsigned frame=s->r[7],matched=0;
    for(unsigned n=0;n<6;n++){
        uint32_t pair[2];
        if((frame&3U) || frame<s->sp || frame>0x3ffffff8U || frame-s->sp>131072U ||
           copyin((user_addr_t)frame,pair,8))break;
        if((pair[1]&~1U)==0x31656a16U+slide){matched=n+1;break;}
        if(pair[0]<=frame)break;frame=pair[0];
    }
    if(!matched || !OSCompareAndSwap(0U,1U,&i9100_media_semaphore_claim))return;
    i9100_media_semaphore_diag[1]=1;i9100_media_semaphore_diag[2]=128;i9100_media_semaphore_diag[3]=16;
    i9100_media_semaphore_diag[4]=i9100_audio_property_lock_history[2];
    i9100_media_semaphore_diag[5]=(unsigned)(uintptr_t)task;i9100_media_semaphore_diag[6]=semaphore;
    i9100_media_semaphore_diag[7]=name;i9100_media_semaphore_target=semaphore;
    i9100_media_semaphore_record(1,semaphore,0,0,0,0,matched);
    /* Capture before asserting the wait, on this exact unblocked trap.
     * Signal records may arrive during a copyin fault, preserving preposts. */
    volatile uint32_t *d=i9100_media_dispatch_wait;uint32_t queue=0;
    d[1]=1;d[2]=i9100_media_semaphore_diag[4];d[3]=(unsigned)(uintptr_t)task;d[4]=slide;
    d[5]=0x39494dd0U+slide;d[6]=copyin((user_addr_t)d[5],&queue,4);d[7]=queue;d[8]=UINT32_MAX;
    if(!d[6] && queue>=4096U && queue<=0x3fffff00U){
        d[8]=copyin((user_addr_t)queue,(void *)&d[16],256);if(!d[8])d[9]=256;
    }
    d[10]=name;d[11]=semaphore;d[12]=(unsigned)(uintptr_t)current_thread();d[13]=matched;
    d[14]=s->pc;d[15]=s->lr;OSMemoryBarrier();d[0]=1;
}
#endif
