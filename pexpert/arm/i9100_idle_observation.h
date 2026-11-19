#ifndef I9100_IDLE_OBSERVATION_H
#define I9100_IDLE_OBSERVATION_H
#include <stdint.h>
/* Same IRQ's MCT counter and completed, IRQ-masked WFI cumulative time.
 * Does not alter scheduler accounting, deadlines, interrupts or power policy. */
struct i9100_idle_observation{
 uint32_t version,sequence,irq,waits,mct_low,mct_high,wait_low,wait_high,last,max,
  frequency,map_ready;
};
static void i9100_idle_publish(volatile struct i9100_idle_observation *out,
 const volatile uint32_t wait[8],uint64_t ticks,unsigned irq,unsigned mapped)
{
 if(!out || !wait || (out->sequence&1U) || wait[0]!=1 || (wait[1]&1U))return;
 uint32_t seq=wait[1],count=wait[2],low=wait[3],high=wait[4],last=wait[5],max=wait[6];
 __sync_synchronize();if(wait[1]!=seq)return;
 out->sequence++;__sync_synchronize();
 out->version=1;out->irq=irq;out->waits=count;out->mct_low=(uint32_t)ticks;
 out->mct_high=(uint32_t)(ticks>>32);out->wait_low=low;out->wait_high=high;
 out->last=last;out->max=max;out->frequency=24000000U;out->map_ready=mapped==1;
 __sync_synchronize();out->sequence++;__sync_synchronize();
}
#endif
