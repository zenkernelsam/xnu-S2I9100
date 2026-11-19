#ifndef I9100_IRQ_PC_SAMPLE_H
#define I9100_IRQ_PC_SAMPLE_H
#include <stdint.h>
enum{I9100_PC_SAMPLE_BINS=64};
struct i9100_pc_sample_bin{uint32_t page,last_pc,mode,count;};
struct i9100_pc_sample{uint32_t version,sequence,samples,used,overflow,period,epoch;
 struct i9100_pc_sample_bin bins[I9100_PC_SAMPLE_BINS];};
static void i9100_pc_sample_record(volatile struct i9100_pc_sample*s,unsigned pc,unsigned cpsr)
{
 if(!s || s->version!=2 || (s->sequence&1U))return;
 unsigned mode=cpsr&31U,page=pc&~255U,slot;
 if(mode!=16U && mode!=19U && mode!=18U)return;
 s->sequence++;__sync_synchronize();
 if(s->samples>=512U){s->samples=0;s->used=0;s->overflow=0;s->epoch++;}
 s->samples++;
 for(slot=0;slot<s->used;slot++)if(s->bins[slot].page==page && s->bins[slot].mode==mode)break;
 if(slot==I9100_PC_SAMPLE_BINS){if(s->overflow!=UINT32_MAX)s->overflow++;}
 else{
  if(slot==s->used){s->bins[slot].page=page;s->bins[slot].mode=mode;s->bins[slot].count=0;s->used++;}
  s->bins[slot].last_pc=pc;
  if(s->bins[slot].count!=UINT32_MAX)s->bins[slot].count++;
 }
 __sync_synchronize();s->sequence++;__sync_synchronize();
}
#endif
