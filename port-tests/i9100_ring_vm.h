/* SPDX-License-Identifier: BSD-2-Clause; independent runtime-allocation ISA VM. */
#ifndef I9100_RING_VM_H
#define I9100_RING_VM_H
#include "i9100_dma_guard.h"
#include <assert.h>
#include <stdint.h>
struct i9100_ring_vm {
 uint32_t *regs,*i2s;
 struct i9100_dma_buffer *buffer[4];
 uint32_t *fifo,capacity,words,pending[512],pending_words;
 uint32_t pc,sar,dar,ccr,word,loaded,loops[2],pause_sev,kill_stuck,delay_gate,drain_stuck;
 unsigned reads,writes,kills,starts;
};
static uint32_t ring_vm_le(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint8_t *ring_vm_memory(struct i9100_ring_vm *v,uint32_t pa,unsigned bytes)
{
 for(unsigned n=0;n<4;n++){struct i9100_dma_buffer *b=v->buffer[n];
  if(b&&pa>=b->physical&&(uint64_t)pa+bytes<=(uint64_t)b->physical+b->bytes){
   assert(b->state==I9100_DMA_DEVICE&&b->holds);return (uint8_t *)b->cpu+(pa-b->physical);}}
 assert(!"hardware VM address outside real private allocation");return 0;
}
static void ring_vm_run(struct i9100_ring_vm *v)
{
 if(!(v->regs[0x100/4]&15U))return;
 for(unsigned step=0;step<20000U;step++){
  v->regs[0x104/4]=v->pc;uint8_t *p=ring_vm_memory(v,v->pc,1);uint8_t op=p[0];
  if(op==0xbc){p=ring_vm_memory(v,v->pc,6);uint32_t word=ring_vm_le(p+2);
   if(p[1]==0)v->sar=word;else if(p[1]==1)v->ccr=word;else {assert(p[1]==2);v->dar=word;}v->pc+=6;
  }else if(op==0x20||op==0x22){p=ring_vm_memory(v,v->pc,2);v->loops[(op>>1)&1U]=p[1];v->pc+=2;
  }else if(op==0x28||op==0x38||op==0x3c){p=ring_vm_memory(v,v->pc,2);assert(p[1]);unsigned loop=(op>>2)&1U;
   if(!(op&16U)||v->loops[loop]){if(op&16U)v->loops[loop]--;v->pc-=p[1];}else v->pc+=2;
  }else if(op==0x35){p=ring_vm_memory(v,v->pc,2);assert(p[1]==12U<<3);v->pc+=2;
  }else if(op==0x32){p=ring_vm_memory(v,v->pc,2);assert(p[1]==12U<<3);
   if(!(v->i2s[0]&5U)){v->regs[0x100/4]=0x207U;return;}v->regs[0x100/4]=0x201U;v->pc+=2;
  }else if(op==4){assert(!v->loaded&&v->ccr==(1U|(2U<<1)|(2U<<15)|(1U<<9)|(1U<<23)));
   v->word=ring_vm_le(ring_vm_memory(v,v->sar,4));v->sar+=4;v->loaded=1;v->pc++;
  }else if(op==0x2b){p=ring_vm_memory(v,v->pc,2);assert(v->loaded&&p[1]==12U<<3&&v->dar==0x03830010U&&v->pending_words<512U);
   v->pending[v->pending_words++]=v->word;v->loaded=0;v->pc+=2;
  }else if(op==0x13){assert(!v->loaded&&v->words+v->pending_words<=v->capacity);
   for(unsigned n=0;n<v->pending_words;n++)v->fifo[v->words++]=v->pending[n];v->pending_words=0;v->pc++;
  }else if(op==0x34){p=ring_vm_memory(v,v->pc,2);assert(p[1]==3U<<3&&!v->pending_words);
   v->regs[0x24/4]|=1U<<3;v->pc+=2;v->regs[0x104/4]=v->pc;v->regs[0x100/4]=0x201U;if(v->pause_sev)return;
  }else if(op==0x36){p=ring_vm_memory(v,v->pc,2);assert(p[1]==4U<<3);v->regs[0x100/4]=0x244U;
   if((v->regs[0x24/4]&(1U<<4))&&!(v->regs[0x20/4]&(1U<<4))&&!v->drain_stuck){v->regs[0x24/4]&=~(1U<<4);v->regs[0x100/4]=0x201U;v->pc+=2;}
   else return;
  }else if(op==0){assert(!v->pending_words&&!v->loaded);v->pc++;v->regs[0x104/4]=v->pc;v->regs[0x100/4]=0;return;
  }else assert(!"unexpected gated-DMA instruction");
 }
 assert(!"VM exceeded one period's instruction bound");
}
static uint32_t ring_vm_read(struct i9100_ring_vm *v,unsigned off)
{v->reads++;return off==0xd08U||off==0xd0cU?0U:v->regs[off/4];}
static void ring_vm_write(struct i9100_ring_vm *v,unsigned off,uint32_t value)
{
 v->writes++;
 if(off==0x2cU){v->regs[0x24/4]&=~(value&v->regs[0x20/4]);return;}
 v->regs[off/4]=value;if(off!=0xd04U)return;
 uint32_t instruction=v->regs[0xd08/4],op=(instruction>>16)&255U;
 if(op==0xa2U){assert(!(instruction&1U));v->starts++;v->pc=v->regs[0xd0c/4];v->regs[0x100/4]=0x201U;
  v->regs[0x104/4]=v->pc;v->sar=v->dar=v->ccr=v->loaded=v->pending_words=0;v->loops[0]=v->loops[1]=0;ring_vm_run(v);
 }else if(op==1U){assert(instruction&1U);assert(!(v->i2s[0]&7U));v->kills++;
  if(!v->kill_stuck){v->regs[0x100/4]=0;v->loaded=v->pending_words=0;v->regs[0x34/4]=0;}
 }else {assert(op==0x34U&&!(instruction&1U)&&((instruction>>24)&255U)==4U<<3);
  v->regs[0x24/4]|=1U<<4;if(!v->delay_gate)ring_vm_run(v);}
}
#endif
