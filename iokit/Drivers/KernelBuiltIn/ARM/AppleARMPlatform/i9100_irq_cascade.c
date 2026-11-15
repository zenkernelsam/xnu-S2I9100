/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_irq_cascade.h"
#include <string.h>
static unsigned base(struct i9100_irq_cascade *c,unsigned child)
{return c->kind==I9100_CASCADE_EINT?(child/8U)*4U:(c->group/4U)*16U;}
static uint32_t bit(struct i9100_irq_cascade *c,unsigned child)
{return 1U<<(c->kind==I9100_CASCADE_EINT?child%8U:(c->group%4U)*8U+child);}
static unsigned region(struct i9100_irq_cascade *c){return c->kind==I9100_CASCADE_EINT?0U:1U;}
static uint32_t rd(struct i9100_irq_cascade *c,unsigned off){return c->io.read32(c->io.cookie,region(c),off);}
static void wr(struct i9100_irq_cascade *c,unsigned off,uint32_t v){c->io.write32(c->io.cookie,region(c),off,v);}
static unsigned is_enabled(struct i9100_irq_cascade *c,unsigned child)
{return c->kind==I9100_CASCADE_EINT?!(rd(c,0xf00U+base(c,child))&bit(c,child)):!!(rd(c,base(c,child))&bit(c,child));}
static unsigned pending(struct i9100_irq_cascade *c,unsigned child)
{return !!(rd(c,(c->kind==I9100_CASCADE_EINT?0xf40U:12U)+base(c,child))&bit(c,child));}
static void mask(struct i9100_irq_cascade *c,unsigned child,unsigned masked)
{
 uint32_t b=bit(c,child);unsigned off=base(c,child);
 if(c->kind==I9100_CASCADE_EINT){off+=0xf00U;wr(c,off,(rd(c,off)&~b)|(masked?b:0U));}
 else wr(c,off+(masked?4U:0U),b);
}
int i9100_cascade_init(struct i9100_irq_cascade *c,const struct i9100_cascade_io *io,enum i9100_cascade_kind kind,unsigned group,uint32_t allowed)
{
 if(!c||c->initialized||!io||!io->read32||!io->write32||!io->enter||!io->leave||!allowed||
    (kind!=I9100_CASCADE_EINT&&kind!=I9100_CASCADE_COMBINER)||
    (kind==I9100_CASCADE_COMBINER&&(group>=16U||(allowed&~255U)))||
    (kind==I9100_CASCADE_EINT&&group))return I9100_CASCADE_ARGUMENT;
 memset(c,0,sizeof(*c));c->io=*io;c->allowed=allowed;c->kind=kind;c->group=group;c->initialized=1;return 0;
}
int i9100_cascade_enable(struct i9100_irq_cascade *c,int enabled)
{
 if(!c||!c->initialized||(enabled!=0&&enabled!=1))return I9100_CASCADE_ARGUMENT;
 uint32_t saved=c->io.enter(c->io.cookie);
 int e=0;if(!enabled)for(unsigned n=0;n<32U;n++)if(c->slots[n].token){e=I9100_CASCADE_BUSY;break;}
 if(!e)c->enabled=enabled;c->io.leave(c->io.cookie,saved);return e;
}
int i9100_cascade_register(struct i9100_irq_cascade *c,unsigned child,unsigned trigger,i9100_cascade_handler handler,void *cookie,struct i9100_cascade_lease *l)
{
 if(!c||!c->initialized||!l||l->active||child>=(c->kind==I9100_CASCADE_EINT?32U:8U)||!handler||
    (c->kind==I9100_CASCADE_EINT?trigger>4U:trigger!=0U))return I9100_CASCADE_ARGUMENT;
 uint32_t saved=c->io.enter(c->io.cookie);int e=0;
 struct i9100_cascade_slot *s=&c->slots[child];
 if(!c->enabled)e=I9100_CASCADE_DISABLED;
 else if(!(c->allowed&(1U<<child)))e=I9100_CASCADE_RESERVED;
 else if(s->token||c->generation==UINT32_MAX)e=I9100_CASCADE_BUSY;
 else if(is_enabled(c,child))e=I9100_CASCADE_BUSY; /* cannot take a running owner's line */
 else {
  memset(s,0,sizeof(*s));s->initial=0;s->token=++c->generation;s->handler=handler;s->cookie=cookie;
  if(c->kind==I9100_CASCADE_EINT){
   unsigned off=0xe00U+base(c,child),shift=(child%8U)*4U;
   uint32_t conf=rd(c,off);s->config=(conf>>shift)&15U;
   wr(c,off,(conf&~(15U<<shift))|(trigger<<shift));
   wr(c,0xf40U+base(c,child),bit(c,child));
  }
  mask(c,child,0);
  if(!is_enabled(c,child))e=I9100_CASCADE_IO;
  memset(l,0,sizeof(*l));l->owner=c;l->child=child;l->token=s->token;l->active=1;
  /* Failed activation keeps ownership for explicit cleanup. */
  if(e)s->fault=1;
 }
 c->io.leave(c->io.cookie,saved);return e;
}
int i9100_cascade_unregister(struct i9100_cascade_lease *l)
{
 if(!l||!l->active||!l->owner||l->child>=32U)return I9100_CASCADE_OWNER;
 struct i9100_irq_cascade *c=l->owner;uint32_t saved=c->io.enter(c->io.cookie);
 struct i9100_cascade_slot *s=&c->slots[l->child];int e=0;
 if(s->token!=l->token)e=I9100_CASCADE_OWNER;
 else if(s->busy)e=I9100_CASCADE_BUSY;
 else {
  mask(c,l->child,1);
  if(is_enabled(c,l->child))e=I9100_CASCADE_IO;
  else {
   if(c->kind==I9100_CASCADE_EINT){
    unsigned off=0xe00U+base(c,l->child),shift=(l->child%8U)*4U;
    wr(c,off,(rd(c,off)&~(15U<<shift))|(s->config<<shift));
    wr(c,0xf40U+base(c,l->child),bit(c,l->child));
   }
   memset(s,0,sizeof(*s));l->active=0;
  }
 }
 c->io.leave(c->io.cookie,saved);return e;
}
int i9100_cascade_dispatch(struct i9100_irq_cascade *c,uint32_t *unowned)
{
 if(!c||!c->initialized||!unowned)return I9100_CASCADE_ARGUMENT;
 *unowned=0;uint32_t saved=c->io.enter(c->io.cookie);
 if(!c->enabled){c->io.leave(c->io.cookie,saved);return I9100_CASCADE_DISABLED;}
 unsigned count=c->kind==I9100_CASCADE_EINT?32U:8U;int delivered=0;
 for(unsigned n=0;n<count;n++) {
  if(!pending(c,n)||!is_enabled(c,n))continue;
  struct i9100_cascade_slot *s=&c->slots[n];
  if(!s->token||s->fault){*unowned|=1U<<n;continue;}
  if(s->busy){mask(c,n,1);s->fault=1;continue;}
  uint32_t token=s->token;s->busy=1;
  if(c->kind==I9100_CASCADE_EINT)wr(c,0xf40U+base(c,n),bit(c,n));
  i9100_cascade_handler handler=s->handler;void *cookie=s->cookie;
  c->io.leave(c->io.cookie,saved);int e=handler(cookie,n);saved=c->io.enter(c->io.cookie);
  if(s->token!=token){c->io.leave(c->io.cookie,saved);return I9100_CASCADE_OWNER;}
  s->busy=0;s->delivered++;delivered++;
  if(e||pending(c,n)){mask(c,n,1);s->fault=1;}
 }
 c->io.leave(c->io.cookie,saved);return delivered;
}
