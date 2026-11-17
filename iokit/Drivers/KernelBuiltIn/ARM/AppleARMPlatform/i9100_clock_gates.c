/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_clock_gates.h"
#include <string.h>
static int lock(struct i9100_clock_gates *m){return m->io.lock(m->io.cookie)?I9100_CLOCK_BUSY:0;}
static void unlock(struct i9100_clock_gates *m){m->io.unlock(m->io.cookie);}
static uint32_t read(struct i9100_clock_gates *m,unsigned gate){return m->io.read32(m->io.cookie,m->gates[gate].config.offset);}
static void set(struct i9100_clock_gates *m,unsigned gate,unsigned enabled)
{struct i9100_clock_gate_config *c=&m->gates[gate].config;m->io.write32(m->io.cookie,c->offset,(read(m,gate)&~c->mask)|(enabled?c->mask:0));}
int i9100_clock_init(struct i9100_clock_gates *m,const struct i9100_clock_io *io,const struct i9100_clock_gate_config *c,unsigned count)
{
 if(!m||!io||!c||m->count||!count||count>I9100_CLOCK_MAX_GATES||!io->read32||!io->write32||!io->lock||!io->unlock)return I9100_CLOCK_ARGUMENT;
 for(unsigned n=0;n<count;n++){
  if((c[n].offset<0xc900U||c[n].offset>0xc970U)&&!(c[n].offset==0xc340U&&c[n].mask==(1U<<12)))return I9100_CLOCK_ARGUMENT;
  if((c[n].offset&3U)||!c[n].mask||(c[n].mask&(c[n].mask-1U)))return I9100_CLOCK_ARGUMENT;
  for(unsigned j=0;j<n;j++)if(c[n].offset==c[j].offset&&(c[n].mask&c[j].mask))return I9100_CLOCK_ARGUMENT;
 }
 memset(m,0,sizeof(*m));m->io=*io;m->count=count;for(unsigned n=0;n<count;n++)m->gates[n].config=c[n];return 0;
}
int i9100_clock_enable(struct i9100_clock_gates *m,int enabled)
{
 if(!m||!m->count||(enabled!=0&&enabled!=1))return I9100_CLOCK_ARGUMENT;
 int e=lock(m);if(e)return e;
 if(!enabled)for(unsigned n=0;n<I9100_CLOCK_MAX_LEASES;n++)if(m->leases[n].token){unlock(m);return I9100_CLOCK_BUSY;}
 m->enabled=(unsigned)enabled;unlock(m);return 0;
}
int i9100_clock_acquire(struct i9100_clock_gates *m,unsigned gate,struct i9100_clock_lease *l)
{
 if(!m||!l||l->active||gate>=m->count)return I9100_CLOCK_ARGUMENT;
 int e=lock(m);if(e)return e;
 if(!m->enabled){unlock(m);return I9100_CLOCK_DISABLED;}
 if(m->gates[gate].fault){unlock(m);return I9100_CLOCK_IO;}
 unsigned slot=0;while(slot<I9100_CLOCK_MAX_LEASES&&m->leases[slot].token)slot++;
 if(slot==I9100_CLOCK_MAX_LEASES||m->generation==UINT32_MAX){unlock(m);return I9100_CLOCK_EXHAUSTED;}
 struct i9100_clock_gate *g=&m->gates[gate];unsigned on=!!(read(m,gate)&g->config.mask);
 if(!g->refs){
  g->initial=on;if(!on)set(m,gate,1);
  if(!(read(m,gate)&g->config.mask)){
   set(m,gate,on);if(!!(read(m,gate)&g->config.mask)!=on)g->fault=1;
   unlock(m);return I9100_CLOCK_IO;
  }
 }else if(!on){g->fault=1;unlock(m);return I9100_CLOCK_IO;}
 memset(l,0,sizeof(*l));l->manager=m;l->slot=slot;l->token=++m->generation;l->active=1;
 m->leases[slot].token=l->token;m->leases[slot].gate=gate;g->refs++;unlock(m);return 0;
}
int i9100_clock_release(struct i9100_clock_lease *l)
{
 if(!l||!l->active||!l->manager||l->slot>=I9100_CLOCK_MAX_LEASES)return I9100_CLOCK_OWNER;
 struct i9100_clock_gates *m=l->manager;int e=lock(m);if(e)return e;
 struct i9100_clock_slot *s=&m->leases[l->slot];
 if(s->token!=l->token||s->gate>=m->count||!m->gates[s->gate].refs){unlock(m);return I9100_CLOCK_OWNER;}
 struct i9100_clock_gate *g=&m->gates[s->gate];
 if(g->refs==1){
  set(m,s->gate,g->initial);
  if(!!(read(m,s->gate)&g->config.mask)!=g->initial){g->fault=1;unlock(m);return I9100_CLOCK_IO;}
 }
 g->refs--;s->token=0;l->active=0;unlock(m);return 0;
}
int i9100_clock_check(struct i9100_clock_lease *l)
{
 if(!l||!l->active||!l->manager||l->slot>=I9100_CLOCK_MAX_LEASES)return I9100_CLOCK_OWNER;
 struct i9100_clock_gates *m=l->manager;int e=lock(m);if(e)return e;struct i9100_clock_slot *s=&m->leases[l->slot];
 if(!m->enabled||s->token!=l->token||s->gate>=m->count||!m->gates[s->gate].refs)e=I9100_CLOCK_OWNER;
 else if(m->gates[s->gate].fault||!(read(m,s->gate)&m->gates[s->gate].config.mask))e=I9100_CLOCK_IO;
 unlock(m);return e;
}
int i9100_clock_recover_idle(struct i9100_clock_gates *m,unsigned gate)
{
 if(!m||gate>=m->count)return I9100_CLOCK_ARGUMENT;int e=lock(m);if(e)return e;
 struct i9100_clock_gate *g=&m->gates[gate];if(!m->enabled||g->refs)e=I9100_CLOCK_BUSY;
 else if(g->fault){
  for(unsigned n=0;n<I9100_CLOCK_MAX_LEASES;n++)if(m->leases[n].token&&m->leases[n].gate==gate)e=I9100_CLOCK_BUSY;
  if(!e&&!!(read(m,gate)&g->config.mask)!=g->initial)e=I9100_CLOCK_IO;
  if(!e)g->fault=0;
 }
 unlock(m);return e;
}
