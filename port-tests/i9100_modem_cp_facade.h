/* SPDX-License-Identifier: BSD-2-Clause */
/* Array-hardware facade for the CP control core: real i9100_gpio_broker over
 * three U1 bank register windows (GPC1 region0/0x80, GPX1 region1/0xc20,
 * GPY4 region1/0x1a0) stored as plain words, plus a write log recording
 * order and model time so tests can verify the vendor step sequence,
 * minimum gaps and neighbor-cell preservation. Explicit injection knobs:
 * dropped writes (readback mismatch), busy lock, frozen/regressing clock. */
#ifndef I9100_MODEM_CP_FACADE_H
#define I9100_MODEM_CP_FACADE_H
#include <assert.h>
#include <stdint.h>
#include <string.h>
enum i9100_cp_slot {CP_SLOT_GPC1,CP_SLOT_GPX1,CP_SLOT_GPY4,CP_SLOTS};
struct i9100_cp_gpio_model {
 uint32_t regs[2][1024];
 unsigned gpio_locked,control_locked,reads,writes,locks,delay_calls;
 unsigned fail_lock,fail_delay,drop_write,drop_from,poison_from,freeze_time;
 uint64_t ns;
 struct {unsigned region,off;uint32_t before,after;uint64_t at;}log[256];unsigned log_n;
 uint64_t pin_time[64]; /* slot*8+pin, last level-change model time */
 uint32_t pin_writes;   /* count of DAT transitions through the log */
};
static const struct {unsigned region,base;}i9100_cp_slot_map[CP_SLOTS]=
 {{0U,0x80U},{1U,0xc20U},{1U,0x1a0U}};
static unsigned i9100_cp_slot_of(unsigned region,unsigned off)
{
 for(unsigned s=0;s<CP_SLOTS;s++)
  if(i9100_cp_slot_map[s].region==region&&off>=i9100_cp_slot_map[s].base&&
     off<i9100_cp_slot_map[s].base+16U)return s;
 return CP_SLOTS;
}
static int cp_gpio_lock(void *cookie)
{struct i9100_cp_gpio_model *p=cookie;if(++p->locks==p->fail_lock||p->gpio_locked)return -1;p->gpio_locked=1;return 0;}
static void cp_gpio_unlock(void *cookie)
{struct i9100_cp_gpio_model *p=cookie;assert(p->gpio_locked);p->gpio_locked=0;}
static uint32_t cp_gpio_read(void *cookie,unsigned region,unsigned off)
{
 struct i9100_cp_gpio_model *p=cookie;
 assert(p->gpio_locked&&region<2U&&off<4096U);p->reads++;return p->regs[region][off/4U];
}
static void cp_gpio_write(void *cookie,unsigned region,unsigned off,uint32_t value)
{
 struct i9100_cp_gpio_model *p=cookie;
 assert(p->gpio_locked&&region<2U&&off<4096U);p->writes++;
 uint32_t *cell=&p->regs[region][off/4U],before=*cell;
 if(p->log_n<sizeof(p->log)/sizeof(p->log[0])){
  p->log[p->log_n].region=region;p->log[p->log_n].off=off;
  p->log[p->log_n].before=before;p->log[p->log_n].after=value;p->log[p->log_n].at=p->ns;p->log_n++;
 }
 if(p->poison_from&&p->writes>=p->poison_from){*cell=~value;return;} /* wrong store -> readback IO */
 if(p->writes==p->drop_write||(p->drop_from&&p->writes>=p->drop_from))return; /* dropped -> readback fails */
 *cell=value;
 unsigned slot=i9100_cp_slot_of(region,off);
 if(slot<CP_SLOTS&&off==i9100_cp_slot_map[slot].base+4U){
  unsigned changed=before^value;
  for(unsigned pin=0;pin<8U;pin++)if(changed&(1U<<pin)){p->pin_time[slot*8U+pin]=p->ns;p->pin_writes++;}
 }
}
static int cp_lock(void *cookie)
{struct i9100_cp_gpio_model *p=cookie;if(p->control_locked)return -1;p->control_locked=1;return 0;}
static void cp_unlock(void *cookie)
{struct i9100_cp_gpio_model *p=cookie;assert(p->control_locked);p->control_locked=0;}
static uint64_t cp_now(void *cookie){return ((struct i9100_cp_gpio_model *)cookie)->ns;}
static int cp_delay(void *cookie,uint32_t us)
{
 struct i9100_cp_gpio_model *p=cookie;assert(p->control_locked);
 if(++p->delay_calls==p->fail_delay)return -1;
 if(!p->freeze_time)p->ns+=(uint64_t)us*1000U;return 0;
}
/* Cold-start preset: every owned pin is an input-low, hostwake (GPX1.1)
 * reads its inactive high level; neighbor pins keep pattern values so tests
 * can prove masked RMW never touched them. */
static void i9100_cp_gpio_cold(struct i9100_cp_gpio_model *p)
{
 for(unsigned s=0;s<CP_SLOTS;s++){
  unsigned base=i9100_cp_slot_map[s].base,region=i9100_cp_slot_map[s].region;
  p->regs[region][base/4U]=0xa5a5a5a5U;
  p->regs[region][(base+4U)/4U]=0x9696dbdbU;
  p->regs[region][(base+8U)/4U]=0xccccccccU;
  p->regs[region][(base+12U)/4U]=0x3f5f7f9fU;
 }
 /* our pins become input(0)+DAT0; neighbors keep AF(0xa)/DAT1 */
 static const unsigned mask[CP_SLOTS]={1U<<1,(1U<<0)|(1U<<1)|(1U<<2)|(1U<<3)|(1U<<4)|(1U<<6),(1U<<2)|(1U<<6)};
 for(unsigned s=0;s<CP_SLOTS;s++){
  unsigned base=i9100_cp_slot_map[s].base,region=i9100_cp_slot_map[s].region;
  for(unsigned pin=0;pin<8U;pin++)if(mask[s]&(1U<<pin)){
   p->regs[region][base/4U]&=~(15U<<(pin*4U));
   p->regs[region][(base+4U)/4U]&=~(1U<<pin);
  }
 }
 p->regs[1][(0xc20U+4U)/4U]|=2U; /* GPX1.1 hostwake idle high */
}
static unsigned i9100_cp_level(struct i9100_cp_gpio_model *p,unsigned slot,unsigned pin)
{return (p->regs[i9100_cp_slot_map[slot].region][(i9100_cp_slot_map[slot].base+4U)/4U]>>pin)&1U;}
static uint64_t i9100_cp_pin_at(struct i9100_cp_gpio_model *p,unsigned slot,unsigned pin)
{return p->pin_time[slot*8U+pin];}
#endif
