/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_irq_registry.h"
#include <string.h>
#define ENABLE_SET 0x100U
#define ENABLE_CLEAR 0x180U
#define PRIORITY 0x400U
#define TARGET 0x800U
#define CONFIG 0xc00U
static uint32_t enter(struct i9100_irq_registry *r){return r->io.enter(r->io.cookie);}
static void leave(struct i9100_irq_registry *r,uint32_t v){r->io.leave(r->io.cookie,v);}
static uint32_t read(struct i9100_irq_registry *r,unsigned off){return r->io.read_dist(r->io.cookie,off);}
static void write(struct i9100_irq_registry *r,unsigned off,uint32_t v){r->io.write_dist(r->io.cookie,off,v);}
static void field(struct i9100_irq_registry *r,unsigned off,unsigned shift,unsigned mask,unsigned value)
{write(r,off,(read(r,off)&~(mask<<shift))|((value&mask)<<shift));}
int i9100_irq_registry_init(struct i9100_irq_registry *r,const struct i9100_irq_io *io,const uint32_t allowed[8])
{
 if(!r||!io||!allowed||!io->read_dist||!io->write_dist||!io->enter||!io->leave||r->enabled||r->generation)return I9100_IRQ_ARGUMENT;
 if(allowed[0])return I9100_IRQ_ARGUMENT; /* interrupt0..31 are private */
 memset(r,0,sizeof(*r));r->io=*io;memcpy(r->allowed,allowed,32);return 0;
}
int i9100_irq_registry_enable(struct i9100_irq_registry *r,int enabled)
{
 if(!r||!r->io.enter||(enabled!=0&&enabled!=1))return I9100_IRQ_ARGUMENT;
 uint32_t saved=enter(r);
 if(!enabled)for(unsigned n=0;n<I9100_IRQ_SLOTS;n++)if(r->slots[n].token){leave(r,saved);return I9100_IRQ_BUSY;}
 r->enabled=(unsigned)enabled;leave(r,saved);return 0;
}
int i9100_irq_register(struct i9100_irq_registry *r,unsigned irq,int edge,i9100_irq_handler handler,void *cookie,struct i9100_irq_lease *l)
{
 if(!r||!l||l->active||!handler||irq<32U||irq>=256U||(edge!=0&&edge!=1)||!(r->allowed[irq/32]&(1U<<(irq&31U))))return I9100_IRQ_ARGUMENT;
 uint32_t saved=enter(r);if(!r->enabled){leave(r,saved);return I9100_IRQ_DISABLED;}
 for(unsigned n=0;n<I9100_IRQ_SLOTS;n++)if(r->slots[n].token&&r->slots[n].irq==irq){leave(r,saved);return I9100_IRQ_BUSY;}
 if(read(r,ENABLE_SET+irq/32U*4U)&(1U<<(irq&31U))){leave(r,saved);return I9100_IRQ_BUSY;}
 unsigned slot=0;while(slot<I9100_IRQ_SLOTS&&r->slots[slot].token)slot++;
 if(slot==I9100_IRQ_SLOTS||r->generation==UINT32_MAX){leave(r,saved);return I9100_IRQ_EXHAUSTED;}
 struct i9100_irq_slot *s=&r->slots[slot];memset(s,0,sizeof(*s));s->irq=irq;
 unsigned byte=(irq&3U)*8U,bits=(irq&15U)*2U;
 s->priority=(read(r,PRIORITY+irq/4U*4U)>>byte)&255U;s->target=(read(r,TARGET+irq/4U*4U)>>byte)&255U;
 s->config=(read(r,CONFIG+irq/16U*4U)>>bits)&3U;
 write(r,ENABLE_CLEAR+irq/32U*4U,1U<<(irq&31U));
 field(r,PRIORITY+irq/4U*4U,byte,255,0x80);field(r,TARGET+irq/4U*4U,byte,255,1);
 field(r,CONFIG+irq/16U*4U,bits,3,edge?2U:0U);
 s->handler=handler;s->cookie=cookie;s->token=++r->generation;
 write(r,ENABLE_SET+irq/32U*4U,1U<<(irq&31U));
 if(!(read(r,ENABLE_SET+irq/32U*4U)&(1U<<(irq&31U)))){
  field(r,PRIORITY+irq/4U*4U,byte,255,s->priority);field(r,TARGET+irq/4U*4U,byte,255,s->target);
  field(r,CONFIG+irq/16U*4U,bits,3,s->config);s->token=0;leave(r,saved);return I9100_IRQ_IO;
 }
 s->enabled=1;l->registry=r;l->slot=slot;l->token=s->token;l->active=1;leave(r,saved);return 0;
}
int i9100_irq_unregister(struct i9100_irq_lease *l)
{
 if(!l||!l->active||!l->registry||l->slot>=I9100_IRQ_SLOTS)return I9100_IRQ_OWNER;
 struct i9100_irq_registry *r=l->registry;uint32_t saved=enter(r);struct i9100_irq_slot *s=&r->slots[l->slot];
 if(s->token!=l->token){leave(r,saved);return I9100_IRQ_OWNER;}
 if(s->busy){leave(r,saved);return I9100_IRQ_BUSY;}
 write(r,ENABLE_CLEAR+s->irq/32U*4U,1U<<(s->irq&31U));
 if(read(r,ENABLE_SET+s->irq/32U*4U)&(1U<<(s->irq&31U))){s->fault=1;leave(r,saved);return I9100_IRQ_IO;}
 unsigned byte=(s->irq&3U)*8U,bits=(s->irq&15U)*2U;
 field(r,PRIORITY+s->irq/4U*4U,byte,255,s->priority);field(r,TARGET+s->irq/4U*4U,byte,255,s->target);
 field(r,CONFIG+s->irq/16U*4U,bits,3,s->config);
 memset(s,0,sizeof(*s));l->active=0;leave(r,saved);return 0;
}
int i9100_irq_dispatch(struct i9100_irq_registry *r,uint32_t acknowledge)
{
 unsigned irq=acknowledge&1023U;if(!r||!r->enabled||irq<32U||irq>=256U)return 0;
 uint32_t saved=enter(r);unsigned index=0;
 while(index<I9100_IRQ_SLOTS&&(!r->slots[index].token||r->slots[index].irq!=irq))index++;
 if(index==I9100_IRQ_SLOTS||!r->slots[index].enabled){leave(r,saved);return 0;}
 struct i9100_irq_slot *s=&r->slots[index];s->busy++;i9100_irq_handler handler=s->handler;void *cookie=s->cookie;
 leave(r,saved);int result=handler(cookie,acknowledge);saved=enter(r);
 s->busy--;if(s->delivered!=UINT32_MAX)s->delivered++;
 if(result<0){
  write(r,ENABLE_CLEAR+irq/32U*4U,1U<<(irq&31U));s->enabled=0;s->fault=1;
  if(read(r,ENABLE_SET+irq/32U*4U)&(1U<<(irq&31U))){s->fault=2;leave(r,saved);return I9100_IRQ_IO;}
 }
 leave(r,saved);return 1;
}
