#include <mach/mach_types.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/queue.h>
#include <arm/machine_routines.h>
#include <libsa/string.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>
#include "../../pexpert/arm/i9100_irq_snapshot.h"
#include "../../pexpert/arm/i9100_media_threads.h"
#if BOARD_CONFIG_EXYNOS4210_I9100
extern unsigned int real_ncpus;
extern volatile uint32_t i9100_audio_property_runtime[4];
extern volatile uint32_t i9100_audio_property_lock_history[1+8*12];
volatile uint32_t i9100_media_threads[16+16*72];
volatile uint32_t i9100_audio_owner_diag[4+8*64];
static volatile UInt32 i9100_media_threads_once;
void i9100_media_threads_note(unsigned pid,unsigned slide)
{
    task_t task=current_task();int enabled=0;
    if(!pid || !task || task==kernel_task || real_ncpus!=1 ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level()!=0 ||
       !PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1 ||
       i9100_audio_property_lock_history[0]<1 || i9100_audio_property_lock_history[2]!=pid ||
       i9100_audio_property_lock_history[3]!=(unsigned)(uintptr_t)task ||
       !OSCompareAndSwap(0U,1U,&i9100_media_threads_once))return;
    volatile uint32_t *out=i9100_media_threads;
    out[1]=2;out[2]=pid;out[3]=(unsigned)(uintptr_t)task;out[6]=slide;
    unsigned count=0,mask;I9100_SNAPSHOT_MASK_GET(mask);out[7]=mask;
    /* No referenced object pointer escapes the raw exclusion. Saved scalar
     * values are evidence only; copyin later uses this still-current task. */
    queue_entry_t entry=NULL;
    if(!i9100_irq_snapshot_read(&task->threads.next,&entry,sizeof(entry)))out[5]=1;
    while(entry && !queue_end(&task->threads,entry) && count<I9100_MEDIA_THREADS_CAP){
        thread_t thread=(thread_t)entry;
        queue_entry_t next=NULL;task_t owner=NULL;arm_saved_state_t *uss=NULL,registers;
        uint64_t tid=0;unsigned state=0;struct uthread *uth=NULL;
        unsigned error=0;
        if(!i9100_irq_snapshot_read(&thread->task_threads.next,&next,sizeof(next)) ||
           !i9100_irq_snapshot_read(&thread->task,&owner,sizeof(owner)) || owner!=task ||
           !i9100_irq_snapshot_read(&thread->thread_id,&tid,sizeof(tid)) ||
           !i9100_irq_snapshot_read(&thread->state,&state,sizeof(state)) ||
           !i9100_irq_snapshot_read(&thread->machine.uss,&uss,sizeof(uss)) || !uss ||
           !i9100_irq_snapshot_read(uss,&registers,sizeof(registers)))error=1;
        volatile uint32_t *r=&out[16+count*I9100_MEDIA_THREADS_STRIDE];
        r[1]=(unsigned)(uintptr_t)thread;r[2]=(uint32_t)tid;r[3]=(uint32_t)(tid>>32);r[4]=state;r[5]=error;
        if(!error){
            memcpy((void *)&r[6],registers.r,13*4);r[19]=registers.sp;r[20]=registers.lr;r[21]=registers.pc;r[22]=registers.cpsr;
            if(i9100_irq_snapshot_read(&thread->uthread,&uth,sizeof(uth)) && uth &&
               i9100_irq_snapshot_read(uth,(void *)&r[60],24))r[66]=1;
        }
        OSMemoryBarrier();r[0]=count+1;count++;
        if(!next || next==entry){out[5]=2;break;}
        entry=next;
    }
    out[4]=count;if(entry && !queue_end(&task->threads,entry) && count==I9100_MEDIA_THREADS_CAP)out[5]=3;
    I9100_SNAPSHOT_MASK_SET(mask);
    for(unsigned n=0;n<count;n++){
        volatile uint32_t *r=&out[16+n*I9100_MEDIA_THREADS_STRIDE];
        if(r[5] || (r[22]&0x1fU)!=0x10U)continue;
        /* Preserve the exact saved PC's original bytes while this task is
         * alive. A low address without its own image map must not be named
         * from another generation's ASLR layout. No user read under masks. */
        uint32_t pc=r[21]&~1U;
        r[67]=(pc>=4096U && pc<=0x3ffffff0U)?
            copyin((user_addr_t)pc,(void *)&r[68],16):UINT32_MAX;
        if((r[18]==301U || r[18]==302U) && r[6]>=4096 && r[6]<=0x3fffffd0U)
            r[24]=copyin((user_addr_t)r[6],(void *)&r[28],48);
        else r[24]=UINT32_MAX;
        uint32_t frame=r[13];unsigned sp=r[19];
        for(unsigned k=0;k<6;k++){
            uint32_t pair[2];
            if((frame&3U) || frame<sp || frame>0x3ffffff8U || frame-sp>131072U){r[25]=1;break;}
            if(copyin((user_addr_t)frame,pair,8)){r[25]=2;break;}
            r[40+k*3]=frame;r[41+k*3]=pair[0];r[42+k*3]=pair[1];r[23]=k+1;
            if(pair[0]<=frame)break;frame=pair[0];
        }
    }
    unsigned runtime=i9100_audio_property_runtime[3];out[8]=runtime;
    if(i9100_audio_property_runtime[1]==pid && i9100_audio_property_runtime[2]==(unsigned)(uintptr_t)task &&
       runtime>=4096 && runtime<=0x3fffffc0U){
        uint32_t words[16];out[9]=copyin((user_addr_t)runtime,words,sizeof(words));
        if(!out[9]){out[10]=words[0];out[11]=words[1];out[12]=words[7];}
    }else out[9]=UINT32_MAX;
    OSMemoryBarrier();out[0]=1;
}

/* Resolve the mutex owner at the wait itself, rather than at a later kill.
 * The first-kill peer list and the first wait may describe different times.
 * Only copied scalars survive the raw exclusion; all own-task user reads
 * follow mask restoration. Never retain/dereference a sampled thread later. */
void i9100_audio_owner_note(unsigned sequence,unsigned pid,unsigned code,
    unsigned mutex,unsigned owner_low,unsigned owner_high)
{
    task_t task=current_task();
    if(sequence<1 || sequence>I9100_AUDIO_OWNER_CAP || !pid || !task || task==kernel_task ||
       real_ncpus!=1 || ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
       get_preemption_level()!=0 || !owner_low || (owner_low==UINT32_MAX && owner_high==UINT32_MAX) ||
       i9100_audio_property_lock_history[2]!=pid ||
       i9100_audio_property_lock_history[3]!=(unsigned)(uintptr_t)task)return;
    volatile uint32_t *r=&i9100_audio_owner_diag[4+(sequence-1)*I9100_AUDIO_OWNER_STRIDE];
    r[1]=pid;r[2]=(unsigned)(uintptr_t)task;r[3]=mutex;r[4]=code;
    r[5]=owner_low;r[6]=owner_high;r[9]=2;r[12]=UINT32_MAX;r[13]=1;
    unsigned mask;I9100_SNAPSHOT_MASK_GET(mask);r[14]=mask;
    queue_entry_t entry=NULL;
    if(!i9100_irq_snapshot_read(&task->threads.next,&entry,sizeof(entry)))r[9]=3;
    for(unsigned n=0;entry && !queue_end(&task->threads,entry) && n<64;n++){
        thread_t thread=(thread_t)entry;queue_entry_t next=NULL;task_t owner=NULL;
        uint64_t tid=0;arm_saved_state_t *uss=NULL,registers;unsigned state=0;
        r[15]=n+1;
        if(!i9100_irq_snapshot_read(&thread->task_threads.next,&next,sizeof(next)) ||
           !i9100_irq_snapshot_read(&thread->task,&owner,sizeof(owner)) || owner!=task ||
           !i9100_irq_snapshot_read(&thread->thread_id,&tid,sizeof(tid))){r[9]=3;break;}
        if((uint32_t)tid==owner_low && (uint32_t)(tid>>32)==owner_high){
            r[7]=(unsigned)(uintptr_t)thread;
            if(i9100_irq_snapshot_read(&thread->state,&state,sizeof(state)) &&
               i9100_irq_snapshot_read(&thread->machine.uss,&uss,sizeof(uss)) && uss &&
               i9100_irq_snapshot_read(uss,&registers,sizeof(registers))){
                r[8]=state;r[9]=0;memcpy((void *)&r[16],registers.r,13*4);
                r[29]=registers.sp;r[30]=registers.lr;r[31]=registers.pc;r[32]=registers.cpsr;
            }else r[9]=3;
            break;
        }
        if(!next || next==entry){r[9]=3;break;}entry=next;
    }
    I9100_SNAPSHOT_MASK_SET(mask);
    if(!r[9] && (r[32]&31U)==16U){
        if((r[28]==301U || r[28]==302U) && r[16]>=4096U && r[16]<=0x3fffffd0U)
            r[12]=copyin((user_addr_t)r[16],(void *)&r[34],48);
        uint32_t frame=r[23];unsigned sp=r[29];
        for(unsigned n=0;n<6;n++){
            uint32_t pair[2];
            if((frame&3U) || frame<sp || frame>0x3ffffff8U || frame-sp>131072U){r[11]=1;break;}
            if(copyin((user_addr_t)frame,pair,8)){r[11]=2;break;}
            r[46+n*3]=frame;r[47+n*3]=pair[0];r[48+n*3]=pair[1];r[10]=n+1;
            if(pair[0]<=frame)break;frame=pair[0];
        }
    }
    I9100_SNAPSHOT_MASK_GET(mask);
    i9100_audio_owner_diag[1]=1;i9100_audio_owner_diag[2]=I9100_AUDIO_OWNER_CAP;
    i9100_audio_owner_diag[3]=I9100_AUDIO_OWNER_STRIDE;
    OSMemoryBarrier();r[0]=sequence;
    if(i9100_audio_owner_diag[0]<sequence)i9100_audio_owner_diag[0]=sequence;
    I9100_SNAPSHOT_MASK_SET(mask);
}
#endif
