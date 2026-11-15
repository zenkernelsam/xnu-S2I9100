#ifndef I9100_AUDIO_TARGET_SELECT_H
#define I9100_AUDIO_TARGET_SELECT_H
#include "../../../pexpert/arm/i9100_audio_observation_target.h"
extern int proc_selfpid(void);
volatile uint32_t i9100_audio_observation_target[8]={1};
int i9100_audio_select_task(unsigned pid,unsigned task)
{
    if(!pid || !task || pid!=(unsigned)proc_selfpid() ||
       task!=(unsigned)(uintptr_t)current_task() || ml_at_interrupt_context() ||
       !ml_get_interrupts_enabled() || get_preemption_level()!=0)return 0;
    volatile uint32_t *t=i9100_audio_observation_target;
    if(t[1]==2){OSMemoryBarrier();return t[2]==pid&&t[3]==task;}
    if(t[1])return 0;
    int enabled=0,abi=0;
    if(!PE_parse_boot_argn("i9100xpcdiag",&enabled,sizeof(enabled)) || enabled!=1 ||
       !PE_parse_boot_argn("i9100audiolock",&enabled,sizeof(enabled)) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,sizeof(abi)) || abi!=6)return 0;
    volatile uint32_t *d=i9100_audio_property_lock_diag,*r=i9100_audio_property_runtime;
    if(d[0]!=2 || d[1]!=pid || d[2]!=task || r[0]!=1 || r[1]!=pid || r[2]!=task)return 0;
    unsigned slide=d[3],address=r[3];uint32_t words[16];
    if((slide&4095U) || slide>=0x40000000U-0x394951c0U ||
       address<4096U || address>0x3fffffc0U ||
       copyin((user_addr_t)address,words,sizeof(words)) || !words[0] ||
       words[1]<2U || words[1]>4U)return 0;
    /* copyin can fault. Check the installation identity again before claiming;
     * no user access occurs while the selection publication is in progress. */
    if(d[0]!=2 || d[1]!=pid || d[2]!=task || d[3]!=slide ||
       r[1]!=pid || r[2]!=task || r[3]!=address ||
       !OSCompareAndSwap(0U,1U,&t[1]))return 0;
    t[2]=pid;t[3]=task;t[4]=slide;t[5]=address;t[6]=words[0];t[7]=words[1];
    OSMemoryBarrier();t[1]=2;return 1;
}
#endif
