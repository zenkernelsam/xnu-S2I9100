/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_radio_power.h"
#include <string.h>
static const unsigned banks[3]={I9100_GPL0,I9100_GPL1,I9100_GPX3};
static const unsigned masks[3]={1U<<4,(1U<<0)|(1U<<2),1U<<1};
static int overlaps(uintptr_t a,unsigned an,uintptr_t b,unsigned bn)
{return a>UINTPTR_MAX-an||b>UINTPTR_MAX-bn||(a+an>b&&b+bn>a);}
static int live(struct i9100_radio_power *p)
{return p&&p->initialized&&p->self==(uintptr_t)p&&p->gpio;}
static int take(struct i9100_radio_power *p)
{return !live(p)?I9100_RADIO_ARGUMENT:p->io.lock(p->io.cookie)?I9100_RADIO_BUSY:0;}
static void drop(struct i9100_radio_power *p){p->io.unlock(p->io.cookie);}
static int gpio_low(struct i9100_radio_power *p,unsigned bank,unsigned pin)
{return i9100_gpio_output(&p->pins[bank],pin,0)?I9100_RADIO_IO:0;}
static int gpio_high(struct i9100_radio_power *p,unsigned bank,unsigned pin)
{return i9100_gpio_output(&p->pins[bank],pin,1)?I9100_RADIO_IO:0;}
static int level(struct i9100_radio_power *p,unsigned bank,unsigned pin,unsigned wanted)
{unsigned value=2;return i9100_gpio_level(&p->pins[bank],pin,&value)||value!=wanted?I9100_RADIO_IO:0;}
static int pause_checked(struct i9100_radio_power *p,uint32_t us)
{
 uint64_t started=p->io.now_ns(p->io.cookie);
 if(p->io.delay_us(p->io.cookie,us))return I9100_RADIO_IO;
 uint64_t now=p->io.now_ns(p->io.cookie),minimum=(uint64_t)us*1000U;
 if(now<started||now-started<minimum||now-started>minimum+100000000ULL)return I9100_RADIO_TIMEOUT;
 return 0;
}
static int off_locked(struct i9100_radio_power *p,unsigned index)
{
 enum i9100_radio_role role=(enum i9100_radio_role)(index+1U);
 if(p->holds[index]||p->io.transports_idle(p->io.cookie,role))return I9100_RADIO_BUSY;
 int e=0,r;
 if(index==0U){
  e=gpio_low(p,1,2);if(!e)e=level(p,1,2,0);
 }else{
  e=gpio_low(p,1,0);r=gpio_low(p,2,1);if(r)e=r;
  r=gpio_low(p,0,4);if(r)e=r;
  r=level(p,1,0,0);if(r)e=r;r=level(p,2,1,0);if(r)e=r;r=level(p,0,4,0);if(r)e=r;
 }
 p->powered[index]=0;
 if(!e)p->power_owned[index]=0;else p->fault=1;
 return e;
}
static int close_locked(struct i9100_radio_power *p)
{
 p->closing=1;p->ready=0;
 if(p->holds[0]||p->holds[1]||p->io.transports_idle(p->io.cookie,I9100_RADIO_WLAN)||
    p->io.transports_idle(p->io.cookie,I9100_RADIO_BT))return I9100_RADIO_BUSY;
 int e=0,r;
 if(p->configured){e=off_locked(p,0);r=off_locked(p,1);if(r)e=r;}
 if(e)return e;
 if(p->supply_owned){
  if(p->io.supply_put(p->io.cookie))return I9100_RADIO_IO;
  p->supply_owned=0;
 }
 p->configured=0; /* outputs/supply safe: later partial KEEP release must not touch already released pins */
 for(unsigned n=0;n<3U;n++)if(p->pins[n].active){
  r=i9100_gpio_release(&p->pins[n],I9100_GPIO_KEEP);if(r)e=I9100_RADIO_IO;
 }
 if(!e)p->enabled=0;
 return e;
}
static int profile_valid(struct i9100_gpio_broker *gpio)
{
 if(!gpio||gpio->count!=I9100_BANK_COUNT||!gpio->enabled)return 0;
 for(unsigned n=0;n<3U;n++){
  const struct i9100_gpio_bank_config *actual=&gpio->banks[banks[n]].config,*expected=&i9100_u1_banks[banks[n]];
  if(actual->offset!=expected->offset||actual->region!=expected->region||actual->pins!=expected->pins||
     actual->reserved_mask!=expected->reserved_mask)return 0;
 }
 return 1;
}
int i9100_radio_power_init(struct i9100_radio_power *p,int enabled,struct i9100_gpio_broker *gpio,const struct i9100_radio_io *io,int verified)
{
 if(!p||p->initialized||(enabled!=0&&enabled!=1))return I9100_RADIO_ARGUMENT;
 if(!enabled)return I9100_RADIO_DISABLED;
 if(verified!=1||!profile_valid(gpio)||!io||!io->lock||!io->unlock||!io->now_ns||!io->delay_us||
    !io->transports_idle||!io->supply_get||!io->supply_verify||!io->supply_put||
    overlaps((uintptr_t)p,sizeof(*p),(uintptr_t)gpio,sizeof(*gpio))||
    overlaps((uintptr_t)p,sizeof(*p),(uintptr_t)io,sizeof(*io)))return I9100_RADIO_ARGUMENT;
 if(io->lock(io->cookie))return I9100_RADIO_BUSY;
 memset(p,0,sizeof(*p));p->io=*io;p->gpio=gpio;p->self=(uintptr_t)p;p->initialized=1;p->stage=1;
 int e=0;
 for(unsigned n=0;n<3U&&!e;n++)if(i9100_gpio_claim(gpio,banks[n],masks[n],&p->pins[n]))e=I9100_RADIO_BUSY;
 /* A cold owner may not disturb an inherited running/floating AF device. */
 for(unsigned n=0;n<3U&&!e;n++)for(unsigned pin=0;pin<8U&&!e;pin++)if(masks[n]&(1U<<pin)){
  unsigned mux=(p->pins[n].saved[0]>>(4U*pin))&15U;
  if(mux>1U||level(p,n,pin,0))e=I9100_RADIO_NOT_READY;
 }
 if(!e){
  p->configured=1;p->stage=2;
  e=gpio_low(p,0,4);if(!e)e=gpio_low(p,1,0);if(!e)e=gpio_low(p,1,2);if(!e)e=gpio_low(p,2,1);
  for(unsigned n=0;n<3U&&!e;n++)for(unsigned pin=0;pin<8U&&!e;pin++)if(masks[n]&(1U<<pin))
   if(i9100_gpio_pull(&p->pins[n],pin,0))e=I9100_RADIO_IO;
 }
 if(!e){
  p->stage=3;unsigned owned=0;int supplied=p->io.supply_get(p->io.cookie,&owned);p->supply_owned=owned?1U:0U;
  if(supplied||owned!=1U)e=I9100_RADIO_IO;
 }
 if(!e&&p->io.supply_verify(p->io.cookie))e=I9100_RADIO_NOT_READY;
 if(!e){p->enabled=p->ready=1;p->stage=4;}
 else{p->error=e;p->cleanup_error=close_locked(p);p->fault=1;}
 drop(p);return e;
}
int i9100_radio_set_power(struct i9100_radio_power *p,enum i9100_radio_role role,int on)
{
 if((role!=I9100_RADIO_WLAN&&role!=I9100_RADIO_BT)||(on!=0&&on!=1))return I9100_RADIO_ARGUMENT;
 int e=take(p);if(e)return e;unsigned index=(unsigned)role-1U;
 if(!p->enabled){drop(p);return I9100_RADIO_DISABLED;}
 if(!on){
  e=p->power_owned[index]?off_locked(p,index):I9100_RADIO_DISABLED;
  p->cleanup_error=e;drop(p);return e;
 }
 if(!p->ready||p->closing){drop(p);return I9100_RADIO_DISABLED;}
 if(p->fault){drop(p);return I9100_RADIO_IO;}
 if(p->power_owned[index]||p->holds[index]||p->generation[index]==UINT32_MAX||p->io.transports_idle(p->io.cookie,role)){
  drop(p);return I9100_RADIO_BUSY;
 }
 if(!p->supply_owned||p->io.supply_verify(p->io.cookie)){p->fault=1;p->error=I9100_RADIO_NOT_READY;drop(p);return p->error;}
 p->power_owned[index]=1;p->stage=5;
 if(index==0U){e=gpio_high(p,1,2);if(!e)e=pause_checked(p,200U);if(!e)e=level(p,1,2,1);}
 else{
  e=gpio_low(p,1,0);if(!e)e=gpio_high(p,0,4);if(!e)e=pause_checked(p,20000U);
  if(!e)e=gpio_high(p,2,1);if(!e)e=gpio_high(p,1,0);if(!e)e=pause_checked(p,50000U);
  if(!e)e=level(p,0,4,1);if(!e)e=level(p,1,0,1);if(!e)e=level(p,2,1,1);
 }
 if(!e&&p->io.supply_verify(p->io.cookie))e=I9100_RADIO_NOT_READY;
 if(!e){p->powered[index]=1;p->generation[index]++;p->stage=6;}
 else{p->error=e;p->cleanup_error=off_locked(p,index);p->fault=1;}
 drop(p);return e;
}
int i9100_radio_hold_get(struct i9100_radio_power *p,enum i9100_radio_role role,struct i9100_radio_hold *h)
{
 if(!h||h->owned||(h->self&&h->self!=(uintptr_t)h)||(role!=I9100_RADIO_WLAN&&role!=I9100_RADIO_BT)||
    !p||overlaps((uintptr_t)p,sizeof(*p),(uintptr_t)h,sizeof(*h)))return I9100_RADIO_ARGUMENT;
 int e=take(p);if(e)return e;unsigned index=(unsigned)role-1U;
 if(!p->enabled||!p->ready||p->closing||!p->powered[index]||!p->power_owned[index])e=I9100_RADIO_DISABLED;
 else if(p->fault)e=I9100_RADIO_IO;
 else if(p->holds[index]==UINT32_MAX)e=I9100_RADIO_BUSY;
 else{
  if(!p->supply_owned||p->io.supply_verify(p->io.cookie))e=I9100_RADIO_NOT_READY;
  if(!e)e=index==0U?level(p,1,2,1):level(p,0,4,1);
  if(!e&&index==1U)e=level(p,1,0,1);if(!e&&index==1U)e=level(p,2,1,1);
  if(e){p->fault=1;p->error=e;p->powered[index]=0;}
  else{memset(h,0,sizeof(*h));h->power=p;h->self=(uintptr_t)h;h->role=role;h->generation=p->generation[index];h->owned=1;p->holds[index]++;}
 }
 drop(p);return e;
}
int i9100_radio_hold_put(struct i9100_radio_hold *h)
{
 if(!h||!h->owned||h->self!=(uintptr_t)h||(h->role!=I9100_RADIO_WLAN&&h->role!=I9100_RADIO_BT))return I9100_RADIO_OWNER;
 int e=take(h->power);if(e)return e;unsigned index=h->role-1U;
 if(h->generation!=h->power->generation[index]||!h->power->holds[index])e=I9100_RADIO_OWNER;
 else{h->power->holds[index]--;h->owned=0;}
 drop(h->power);return e;
}
int i9100_radio_power_status(struct i9100_radio_power *p,struct i9100_radio_status *out)
{
 if(!out||!p||overlaps((uintptr_t)p,sizeof(*p),(uintptr_t)out,sizeof(*out)))return I9100_RADIO_ARGUMENT;
 int e=take(p);if(e)return e;memset(out,0,sizeof(*out));
 out->enabled=p->enabled;out->ready=p->ready;out->closing=p->closing;out->fault=p->fault;out->configured=p->configured;
 out->supply_owned=p->supply_owned;out->stage=p->stage;out->error=p->error;out->cleanup_error=p->cleanup_error;
 for(unsigned n=0;n<3U;n++)out->pins_owned+=p->pins[n].active?1U:0U;
 for(unsigned n=0;n<2U;n++){out->powered[n]=p->powered[n];out->power_owned[n]=p->power_owned[n];out->generation[n]=p->generation[n];out->holds[n]=p->holds[n];}
 drop(p);return 0;
}
int i9100_radio_power_close(struct i9100_radio_power *p)
{
 int e=take(p);if(e)return e;e=close_locked(p);p->cleanup_error=e;if(e)p->fault=1;drop(p);return e;
}
