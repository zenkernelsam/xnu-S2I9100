/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_bcm_chip.h"
/* Independent wire stand-in: real F1 window selectors/4-byte flag and DMP
 * descriptors, not a core-ops mock that bypasses address translation. Core
 * reset, RAM data and radio writes are deliberately absent from this model. */
struct model {
 uint32_t erom[I9100_BCM_EROM_WORDS],words,chip_id,eromptr,coreinfo,bankidx;
 uint32_t banks[I9100_BCM_MAX_BANKS],arm_reset,ram_reset,ram_clock,d11_reset;
 uint8_t window[3],clock;
 unsigned locked,ops,fail_op,ignore_window_restore,ignore_bank_restore,bank_writes;
 uint64_t ns,step;
 uint8_t *ram;uint32_t ram_capacity,corrupt_ram_address;
 unsigned ram_writes,ram_reads,token_writes,ignore_token_write,ignore_ram_write;
 uint8_t *mutate_source;
 unsigned core_actions_allowed,core_writes,drop_core_control,reset_stall,reset_status_busy,freeze_time,defer_reset_reads;
 uint32_t cm3_control,d11_control,pending_value[3];unsigned pending_reads[3];
 unsigned boot_model,ht_ready,f2_ready,no_mailbox,mailbox_delay,event_raised,mailbox_interrupt,mailbox_ack_count;
 uint32_t mailbox_pre_data;
 uint32_t post_clear_irq,post_ack_irq,post_ack_mailbox;
 uint32_t sdio_regs[32],firmware_mailbox,expected_token;
 uint8_t function_enable,fbr_low,fbr_high;
};
static struct model m;
static struct i9100_bcm4330 bus;
static struct i9100_bcm_chip chip;
static int lock(void *cookie){struct model *p=(struct model *)cookie;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *cookie){struct model *p=(struct model *)cookie;assert(p->locked);p->locked=0;}
static uint64_t now(void *cookie){return ((struct model *)cookie)->ns;}
static int delay(void *cookie,uint32_t us){struct model *p=(struct model *)cookie;if(!p->freeze_time)p->ns+=(uint64_t)us*1000U;return 0;}
static int operation(struct model *p){p->ns+=p->step;return ++p->ops==p->fail_op?-1:0;}
static int cmd52(void *cookie,int write,unsigned fn,uint32_t addr,uint8_t *v)
{
 struct model *p=(struct model *)cookie;assert(p->locked&&fn==1U);
 if(addr>=0x1000aU&&addr<=0x1000cU){
  unsigned n=addr-0x1000aU;const uint8_t original[3]={0x80,0x70,0x16};
  if(write){if(!(p->ignore_window_restore&&*v==original[n]))p->window[n]=*v;}
  else *v=p->window[n];
 }else{assert(addr==0x1000eU);if(write)p->clock=*v;else *v=p->clock|0x40U|((p->boot_model&&p->ht_ready&&(p->clock&0x12U))?0x80U:0U);}
 return operation(p);
}
static uint32_t word_read(struct model *p,uint32_t address)
{
 if(address==0x18000000U)return p->chip_id;
 if(address==0x180000fcU)return p->eromptr;
 if(address>=0x18110000U&&address<0x18110e00U)return p->erom[(address-0x18110000U)/4U];
 const uint32_t wrappers[3]={0x18102000U,0x18101000U,0x18104000U};
 for(unsigned n=0;n<3U;n++){
  if(address==wrappers[n]+0x804U)return p->reset_status_busy==n+1U?1U:0U;
  if(address==wrappers[n]+0x800U){
   uint32_t *reset=n==0?&p->arm_reset:n==1?&p->d11_reset:&p->ram_reset;
   if(p->pending_reads[n]&&!--p->pending_reads[n])*reset=p->pending_value[n];return *reset;
  }
 }
 if(address==0x18102408U)return p->cm3_control;
 if(address==0x18101408U)return p->d11_control;
 if(address==0x18104408U)return p->ram_clock;
 if(address==0x18004000U)return p->coreinfo;
 if(address==0x18004010U)return p->bankidx;
 if(address==0x18004040U){assert(p->bankidx<I9100_BCM_MAX_BANKS);return p->banks[p->bankidx];}
 if(p->boot_model&&address>=0x18011000U&&address<0x18011080U){
  if(address==0x18011020U&&!(p->arm_reset&1U)&&(p->function_enable&4U)&&p->f2_ready&&!p->event_raised&&!p->no_mailbox){
   if(p->mailbox_delay)p->mailbox_delay--;
   else{p->sdio_regs[0x20U/4U]|=p->mailbox_interrupt?p->mailbox_interrupt:0x80U;
    p->sdio_regs[0x4cU/4U]=p->mailbox_pre_data&&!p->mailbox_ack_count?p->mailbox_pre_data:p->firmware_mailbox;
    p->event_raised=1;p->d11_reset&=~1U;}
  }
  return p->sdio_regs[(address-0x18011000U)/4U];
 }
 assert(!"Unreviewed register/ROM/RAM read");return 0;
}
static int cmd53(void *cookie,int write,unsigned fn,uint32_t offset,int increment,uint8_t *bytes,uint32_t count)
{
 struct model *p=(struct model *)cookie;assert(p->locked&&fn==1U&&count&&count<=512U&&!(count&3U)&&increment&&(offset&0x8000U));
 uint32_t address=((uint32_t)p->window[2]<<24)|((uint32_t)p->window[1]<<16)|
                  ((uint32_t)(p->window[0]&0x80U)<<8)|(offset&0x7fffU);
 if(address<4U*1024U*1024U){
  assert(p->ram&&(uint64_t)address+count<=p->ram_capacity&&(address&0x7fffU)+count<=0x8000U);
  if(write){
   p->ram_writes++;
   if(address==p->ram_capacity-4U){p->token_writes++;if(!p->ignore_token_write)memcpy(p->ram+address,bytes,count);}
   else if(!p->ignore_ram_write)memcpy(p->ram+address,bytes,count);
   if(address==0U&&p->mutate_source){p->mutate_source[0]^=1U;p->mutate_source=0;}
  }else{
   p->ram_reads++;memcpy(bytes,p->ram+address,count);
   if(p->corrupt_ram_address>=address&&p->corrupt_ram_address<address+count)bytes[p->corrupt_ram_address-address]^=1U;
  }
  return operation(p);
 }
 assert(count==4U);
 if(write){
  uint32_t v=bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
  if(address==0x18004010U){p->bank_writes++;if(!(p->ignore_bank_restore&&v==0x101U))p->bankidx=v;}
  else if(p->boot_model&&address>=0x18011000U&&address<0x18011080U){
   unsigned off=address-0x18011000U;assert(off==0x20U||off==0x40U||off==0x48U);
   if(off==0x20U){p->sdio_regs[off/4U]&=~v;
    if(p->post_clear_irq){p->sdio_regs[off/4U]|=p->post_clear_irq;p->post_clear_irq=0;}
   }
   else if(off==0x40U){assert(v==2U);p->sdio_regs[0x20U/4U]&=~0x80U;p->mailbox_ack_count++;
    if(p->mailbox_pre_data&&p->mailbox_ack_count==1U){
     /* A real producer can post the next mailbox as soon as ACK is written. */
     p->sdio_regs[0x4cU/4U]=p->firmware_mailbox;p->sdio_regs[0x20U/4U]|=0x80U;
    }
    if(p->post_ack_irq){p->sdio_regs[0x20U/4U]|=p->post_ack_irq;p->post_ack_irq=0;
     if(p->post_ack_mailbox)p->sdio_regs[0x4cU/4U]=p->post_ack_mailbox;
    }
   }
   else p->sdio_regs[off/4U]=v;
  }
  else{
   assert(p->core_actions_allowed); /* original suites continue to forbid every core write */
   const uint32_t wrappers[3]={0x18102000U,0x18101000U,0x18104000U};unsigned accepted=0;
   for(unsigned n=0;n<3U;n++){
    if(address==wrappers[n]+0x408U){
     uint32_t *control=n==0?&p->cm3_control:n==1?&p->d11_control:&p->ram_clock;
     if(!p->drop_core_control)*control=v;accepted=1;
    }
    if(address==wrappers[n]+0x800U){
     if(p->boot_model&&n==0U&&!(v&1U)){
      assert(p->ram&&p->expected_token);uint8_t *q=p->ram+p->ram_capacity-4U;
      assert((q[0]|((uint32_t)q[1]<<8)|((uint32_t)q[2]<<16)|((uint32_t)q[3]<<24))==p->expected_token);
      assert((p->sdio_regs[0x48U/4U]&0xff0000U)==0x40000U&&!(p->sdio_regs[0x20U/4U]&0xf0U));
     }
     uint32_t *reset=n==0?&p->arm_reset:n==1?&p->d11_reset:&p->ram_reset;
     if(p->reset_stall!=n+1U){
      if(p->defer_reset_reads){p->pending_value[n]=v;p->pending_reads[n]=p->defer_reset_reads;}
      else *reset=v;
     }
     accepted=1;
    }
   }
   assert(accepted);p->core_writes++;
  }
 }else{uint32_t v=word_read(p,address);for(unsigned n=0;n<4;n++)bytes[n]=(uint8_t)(v>>(8U*n));}
 return operation(p); /* side effect may happen even when the transaction fails */
}
static int send_frame(void *cookie,const uint8_t *data,uint32_t n)
{(void)cookie;(void)data;(void)n;assert(!"Unexpected F2/firmware action");return -1;}
static void core(uint16_t id,uint8_t rev,uint32_t base,uint32_t wrapper,int master)
{
 assert(m.words+5U<I9100_BCM_EROM_WORDS);
 m.erom[m.words++]=0x4bf00001U|((uint32_t)id<<8);
 m.erom[m.words++]=((uint32_t)rev<<24)|(1U<<9)|(master?(1U<<14)|(1U<<4):(1U<<19))|1U;
 if(master)m.erom[m.words++]=3U;
 m.erom[m.words++]=base|5U;m.erom[m.words++]=wrapper|(master?0xc5U:0x85U);
}
static void setup(unsigned ramrev)
{
 memset(&m,0,sizeof(m));memset(&bus,0,sizeof(bus));memset(&chip,0,sizeof(chip));
 m.chip_id=0x10034330U;m.eromptr=0x18110000U;m.coreinfo=0x30U;m.bankidx=0x101U;
 m.arm_reset=1;m.ram_clock=1;m.banks[0]=15;m.banks[1]=0x1000f;m.banks[2]=15;
 m.d11_reset=1;
 m.corrupt_ram_address=0xffffffffU;
 m.window[0]=0x80;m.window[1]=0x70;m.window[2]=0x16;
 core(0x800,35,0x18000000U,0x18100000U,0);
 core(0x82a,7,0x18002000U,0x18102000U,1);
 core(0x80e,(uint8_t)ramrev,0x18004000U,0x18104000U,0);
 core(0x829,12,0x18011000U,0x18111000U,0);
 m.erom[m.words++]=15U;
 struct i9100_bcm_io io={&m,lock,unlock,cmd52,cmd53,send_frame,now,delay};
 assert(!i9100_bcm_init(&bus,&io));assert(!i9100_bcm_enable(&bus,1,0x2d0,0x4330));
 assert(!i9100_bcm_chip_init(&chip,&bus));
}
static void window_original(void)
{assert(m.window[0]==0x80U&&m.window[1]==0x70U&&m.window[2]==0x16U&&!bus.window_restore_pending);}
static void finish(void)
{
 assert(!i9100_bcm_chip_ram_restore(&chip));assert(!chip.ram_selector_owned);
 assert(!i9100_bcm_chip_close(&chip));assert(!bus.chip_holds);
 assert(!i9100_bcm_enable(&bus,0,0,0));window_original();assert(!bus.enabled&&!m.locked);
}
