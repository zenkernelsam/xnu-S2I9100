/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_gpio_broker.h"
#include <string.h>
static int enter(struct i9100_gpio_broker *b){return b->io.lock(b->io.cookie)?I9100_GPIO_BUSY:0;}
static void leave(struct i9100_gpio_broker *b){b->io.unlock(b->io.cookie);}
static uint32_t readreg(struct i9100_gpio_broker *b,unsigned bank,unsigned off)
{struct i9100_gpio_bank_config *c=&b->banks[bank].config;return b->io.read32(b->io.cookie,c->region,c->offset+off);}
static void update(struct i9100_gpio_broker *b,unsigned bank,unsigned off,uint32_t mask,uint32_t value)
{struct i9100_gpio_bank_config *c=&b->banks[bank].config;b->io.write32(b->io.cookie,c->region,c->offset+off,(readreg(b,bank,off)&~mask)|(value&mask));}
int i9100_gpio_broker_init(struct i9100_gpio_broker *b,const struct i9100_gpio_io *io,const struct i9100_gpio_bank_config *cfg,unsigned count)
{
 if(!b||!io||!cfg||b->count||!count||count>I9100_GPIO_BANKS_MAX||!io->read32||!io->write32||!io->lock||!io->unlock)return I9100_GPIO_ARGUMENT;
 for(unsigned n=0;n<count;n++){
  if(cfg[n].region>1||!cfg[n].pins||cfg[n].pins>8||(cfg[n].offset&31U)||
     (cfg[n].offset>0x3e0U&&!(cfg[n].region==1&&cfg[n].offset>=0xc00U&&cfg[n].offset<=0xc60U))||
     cfg[n].reserved_mask&~((1U<<cfg[n].pins)-1U))return I9100_GPIO_ARGUMENT;
  for(unsigned j=0;j<n;j++)if(cfg[n].region==cfg[j].region&&cfg[n].offset==cfg[j].offset)return I9100_GPIO_ARGUMENT;
 }
 memset(b,0,sizeof(*b));b->io=*io;b->count=count;
 for(unsigned n=0;n<count;n++)b->banks[n].config=cfg[n];
 return 0;
}
int i9100_gpio_broker_enable(struct i9100_gpio_broker *b,int enabled)
{
 if(!b||!b->count||(enabled!=0&&enabled!=1))return I9100_GPIO_ARGUMENT;
 int e=enter(b);if(e)return e;
 if(!enabled)for(unsigned n=0;n<b->count;n++)for(unsigned p=0;p<8;p++)if(b->banks[n].owners[p]){leave(b);return I9100_GPIO_BUSY;}
 b->enabled=(unsigned)enabled;leave(b);return 0;
}
int i9100_gpio_claim(struct i9100_gpio_broker *b,unsigned bank,unsigned mask,struct i9100_gpio_lease *l)
{
 if(!b||!l||l->active||bank>=b->count||!mask||mask&~((1U<<b->banks[bank].config.pins)-1U))return I9100_GPIO_ARGUMENT;
 int e=enter(b);if(e)return e;
 if(!b->enabled){leave(b);return I9100_GPIO_DISABLED;}
 if(mask&b->banks[bank].config.reserved_mask){leave(b);return I9100_GPIO_RESERVED;}
 for(unsigned p=0;p<8;p++)if((mask&(1U<<p))&&b->banks[bank].owners[p]){leave(b);return I9100_GPIO_BUSY;}
 if(b->generation==UINT32_MAX){leave(b);return I9100_GPIO_EXHAUSTED;}
 memset(l,0,sizeof(*l));l->broker=b;l->bank=bank;l->mask=mask;l->generation=++b->generation;
 for(unsigned n=0;n<4;n++)l->saved[n]=readreg(b,bank,n*4U);
 for(unsigned p=0;p<8;p++)if(mask&(1U<<p))b->banks[bank].owners[p]=l->generation;
 l->active=1;leave(b);return 0;
}
static int owner(struct i9100_gpio_lease *l,unsigned pin)
{
 if(!l||!l->active||!l->broker||l->bank>=l->broker->count||pin>=l->broker->banks[l->bank].config.pins||!(l->mask&(1U<<pin)))return I9100_GPIO_OWNER;
 if(!l->broker->enabled||l->broker->banks[l->bank].owners[pin]!=l->generation)return I9100_GPIO_OWNER;
 return 0;
}
static int setfield(struct i9100_gpio_lease *l,unsigned pin,unsigned off,unsigned width,unsigned value)
{
 if(!l||!l->broker||value>=(1U<<width))return I9100_GPIO_ARGUMENT;
 struct i9100_gpio_broker *b=l->broker;int e=enter(b);if(e)return e;
 e=owner(l,pin);
 if(!e)update(b,l->bank,off,((1U<<width)-1U)<<(pin*width),value<<(pin*width));
 leave(b);return e;
}
int i9100_gpio_input(struct i9100_gpio_lease *l,unsigned pin){return setfield(l,pin,0,4,0);}
int i9100_gpio_function(struct i9100_gpio_lease *l,unsigned pin,unsigned value){return setfield(l,pin,0,4,value);}
int i9100_gpio_pull(struct i9100_gpio_lease *l,unsigned pin,unsigned value)
{if(value==2||value>3)return I9100_GPIO_ARGUMENT;return setfield(l,pin,8,2,value);}
int i9100_gpio_drive(struct i9100_gpio_lease *l,unsigned pin,unsigned value){return setfield(l,pin,12,2,value);}
int i9100_gpio_output(struct i9100_gpio_lease *l,unsigned pin,unsigned value)
{
 if(!l||!l->broker||value>1)return I9100_GPIO_ARGUMENT;
 struct i9100_gpio_broker *b=l->broker;int e=enter(b);if(e)return e;
 e=owner(l,pin);
 if(!e){update(b,l->bank,4,1U<<pin,value<<pin);update(b,l->bank,0,15U<<(pin*4),1U<<(pin*4));}
 leave(b);return e;
}
int i9100_gpio_level(struct i9100_gpio_lease *l,unsigned pin,unsigned *value)
{
 if(!l||!l->broker||!value)return I9100_GPIO_ARGUMENT;
 struct i9100_gpio_broker *b=l->broker;int e=enter(b);if(e)return e;
 e=owner(l,pin);if(!e)*value=(readreg(b,l->bank,4)>>pin)&1U;leave(b);return e;
}
int i9100_gpio_release(struct i9100_gpio_lease *l,enum i9100_gpio_release_policy policy)
{
 if(!l||!l->broker||!l->active||policy>I9100_GPIO_HIGH_Z||policy<I9100_GPIO_RESTORE)return I9100_GPIO_ARGUMENT;
 struct i9100_gpio_broker *b=l->broker;int e=enter(b);if(e)return e;
 uint32_t mux=0,pull=0;
 for(unsigned p=0;p<8;p++)if(l->mask&(1U<<p)){
  e=owner(l,p);if(e){leave(b);return e;}mux|=15U<<(p*4);pull|=3U<<(p*2);
 }
 if(policy!=I9100_GPIO_KEEP){
  update(b,l->bank,0,mux,0); /* release before restoring an output latch */
  if(policy==I9100_GPIO_RESTORE){
   update(b,l->bank,4,l->mask,l->saved[1]);update(b,l->bank,8,pull,l->saved[2]);
   update(b,l->bank,12,pull,l->saved[3]);update(b,l->bank,0,mux,l->saved[0]);
  }else update(b,l->bank,8,pull,0);
 }
 for(unsigned p=0;p<8;p++)if(l->mask&(1U<<p))b->banks[l->bank].owners[p]=0;
 l->active=0;leave(b);return 0;
}
