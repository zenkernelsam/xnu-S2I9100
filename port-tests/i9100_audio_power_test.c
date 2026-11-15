/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct model {
 uint32_t regs[4096],locked,reads,writes,fail_write,sticky_write,freeze,backward,delay_fail;
 unsigned lease_bad,busy,temp_owned,temp_gets,temp_puts,get_fail,put_fail,bus_fail,bus_reads,stall;
 uint64_t ns,transition;
};
static struct model m;
static int lock(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint32_t read(void *c,unsigned off)
{struct model *p=c;assert(p->locked&&(off==0xa00U||off==0x3cc0U||off==0x3cc4U));p->reads++;
 if(off==0x3cc4U&&!p->stall&&p->ns>=p->transition)p->regs[off/4]=(p->regs[off/4]&~7U)|(p->regs[0x3cc0/4]&7U);
 return p->regs[off/4];}
static void write(void *c,unsigned off,uint32_t value)
{struct model *p=c;assert(p->locked&&(off==0xa00U||off==0x3cc0U));p->writes++;
 if(off==0x3cc0U)assert(p->temp_owned);if(p->writes==p->fail_write||p->sticky_write)return;
 p->regs[off/4]=value;if(off==0x3cc0U)p->transition=p->ns+10000ULL;}
static uint64_t now(void *c){struct model *p=c;if(p->backward)return p->ns--;
 return p->ns;}
static int delay(void *c,uint32_t us){struct model *p=c;assert(p->locked);if(!p->freeze)p->ns+=us*1000ULL;return p->delay_fail?-1:0;}
static int valid_lease(void *c){struct model *p=c;assert(p->locked);return p->lease_bad?-1:0;}
static int idle(void *c){struct model *p=c;assert(p->locked);return p->busy?-1:0;}
static int temp_get(void *c){struct model *p=c;assert(p->locked&&!p->temp_owned);p->temp_owned=1;p->temp_gets++;return p->get_fail?-1:0;}
static int temp_put(void *c){struct model *p=c;assert(p->locked);p->temp_puts++;if(p->put_fail)return -1;p->temp_owned=0;return 0;}
static int confirm_bus(void *c){struct model *p=c;assert(p->locked&&p->temp_owned&&(p->regs[0x3cc4/4]&7U)==7U);p->bus_reads++;return p->bus_fail?-1:0;}
static struct i9100_audio_pmu_io io={&m,read,write,lock,unlock,now,delay,valid_lease,idle,temp_get,temp_put,confirm_bus};
static void setup(void){memset(&m,0,sizeof(m));m.regs[0xa00/4]=0xa55a0001;m.regs[0x3cc0/4]=0x12340000;m.regs[0x3cc4/4]=0xabc00000;}
static void mclk(void)
{
 struct i9100_audio_mclk c;
 for(unsigned source=0;source<16;source++)for(unsigned disabled=0;disabled<2;disabled++){
  setup();memset(&c,0,sizeof(c));m.regs[0xa00/4]=0xa55a0000U|(source<<8)|disabled;uint32_t original=m.regs[0xa00/4];
  assert(!i9100_audio_mclk_init(&c,&io,24000000));assert(!m.reads&&!m.writes);
  assert(i9100_audio_mclk_acquire(&c)==I9100_AP_DISABLED&&!m.reads&&!m.writes);
  assert(!i9100_audio_mclk_enable(&c,1));
  if(!disabled&&source!=9U){assert(i9100_audio_mclk_acquire(&c)==I9100_AP_BUSY&&!c.owned&&!m.writes);continue;}
  assert(!i9100_audio_mclk_acquire(&c)&&c.owned&&c.ready&&m.ns>=10000000ULL);
  assert(m.regs[0xa00/4]==((original&~0xf01U)|0x900U));
  uint32_t hz=0;assert(!i9100_audio_mclk_rate(&c,&hz)&&hz==24000000U);
  assert(!i9100_audio_mclk_hold(&c));unsigned writes=m.writes;
  assert(i9100_audio_mclk_release(&c)==I9100_AP_BUSY&&m.writes==writes);
  assert(i9100_audio_mclk_enable(&c,0)==I9100_AP_BUSY);assert(!i9100_audio_mclk_unhold(&c));
  m.busy=1;assert(i9100_audio_mclk_release(&c)==I9100_AP_BUSY&&m.writes==writes);m.busy=0;
  struct i9100_audio_mclk copy=c;assert(i9100_audio_mclk_release(&copy)==I9100_AP_OWNER&&m.writes==writes);
  assert(!i9100_audio_mclk_release(&c)&&m.regs[0xa00/4]==original&&!c.owned);assert(!i9100_audio_mclk_enable(&c,0));
 }
 for(unsigned failure=0;failure<4;failure++){
  setup();memset(&c,0,sizeof(c));assert(!i9100_audio_mclk_init(&c,&io,24000000));assert(!i9100_audio_mclk_enable(&c,1));
  if(failure==0)m.fail_write=1;else if(failure==1)m.freeze=1;else if(failure==2)m.delay_fail=1;else{m.ns=100000000;m.backward=1;}
  assert(i9100_audio_mclk_acquire(&c)&&c.owned&&c.fault&&!c.ready);
  m.freeze=m.delay_fail=m.backward=0;assert(!i9100_audio_mclk_release(&c)&&m.regs[0xa00/4]==0xa55a0001);
 }
 setup();memset(&c,0,sizeof(c));assert(!i9100_audio_mclk_init(&c,&io,24000000));assert(!i9100_audio_mclk_enable(&c,1));assert(!i9100_audio_mclk_acquire(&c));
 m.regs[0xa00/4]^=1U;uint32_t hz=99;assert(i9100_audio_mclk_rate(&c,&hz)==I9100_AP_IO&&!hz&&c.owned&&c.fault);
 assert(!i9100_audio_mclk_release(&c));
 setup();memset(&c,0,sizeof(c));assert(!i9100_audio_mclk_init(&c,&io,24000000));assert(!i9100_audio_mclk_enable(&c,1));assert(!i9100_audio_mclk_acquire(&c));
 m.sticky_write=1;assert(i9100_audio_mclk_release(&c)==I9100_AP_IO&&c.owned&&c.fault);m.sticky_write=0;assert(!i9100_audio_mclk_release(&c));
 setup();memset(&c,0,sizeof(c));assert(i9100_audio_mclk_init(&c,&io,19200000)==I9100_AP_ARGUMENT&&!m.reads&&!m.writes);
}
static void domain(void)
{
 struct i9100_audio_domain d;
 for(unsigned on=0;on<2;on++){
  setup();memset(&d,0,sizeof(d));m.regs[0x3cc0/4]|=on?7U:0;m.regs[0x3cc4/4]|=on?7U:0;
  assert(!i9100_audio_domain_init(&d,&io));assert(!m.reads&&!m.writes);
  assert(i9100_audio_domain_acquire(&d)==I9100_AP_DISABLED&&!m.reads&&!m.writes);assert(!i9100_audio_domain_enable(&d,1));
  assert(!i9100_audio_domain_acquire(&d)&&d.ready&&d.owned&&!d.temp_owned&&!m.temp_owned&&m.bus_reads==1);
  assert(m.regs[0x3cc0/4]==0x12340007U&&(m.regs[0x3cc4/4]&7U)==7U);
  assert(!i9100_audio_domain_hold(&d));unsigned writes=m.writes;
  assert(i9100_audio_domain_release(&d)==I9100_AP_BUSY&&m.writes==writes);assert(!i9100_audio_domain_unhold(&d));
  m.busy=1;assert(i9100_audio_domain_release(&d)==I9100_AP_BUSY&&m.writes==writes);m.busy=0;
  assert(!i9100_audio_domain_release(&d)&&!d.owned&&!d.temp_owned&&!m.temp_owned);
  assert(m.regs[0x3cc0/4]==(0x12340000U|(on?7U:0))&&(m.regs[0x3cc4/4]&7U)==(on?7U:0));
 }
 for(unsigned failure=0;failure<6;failure++){
  setup();memset(&d,0,sizeof(d));assert(!i9100_audio_domain_init(&d,&io));assert(!i9100_audio_domain_enable(&d,1));
  switch(failure){case 0:m.get_fail=1;break;case 1:m.fail_write=1;break;case 2:m.stall=1;break;
   case 3:m.bus_fail=1;break;case 4:m.put_fail=1;break;case 5:m.stall=m.freeze=1;break;}
  assert(i9100_audio_domain_acquire(&d)&&d.owned&&d.fault&&!d.ready&&d.temp_owned);
  assert(i9100_audio_domain_enable(&d,0)==I9100_AP_BUSY);
  m.get_fail=m.stall=m.bus_fail=m.put_fail=m.freeze=0;assert(!i9100_audio_domain_release(&d));
  assert(!d.owned&&!d.temp_owned&&!m.temp_owned&&(m.regs[0x3cc4/4]&7U)==0U);
 }
 for(unsigned failure=0;failure<4;failure++){
  setup();memset(&d,0,sizeof(d));assert(!i9100_audio_domain_init(&d,&io));assert(!i9100_audio_domain_enable(&d,1));assert(!i9100_audio_domain_acquire(&d));
  if(failure==0)m.get_fail=1;else if(failure==1)m.fail_write=m.writes+1;else if(failure==2)m.stall=1;else m.put_fail=1;
  assert(i9100_audio_domain_release(&d)&&d.owned&&d.temp_owned&&d.fault);
  m.get_fail=m.stall=m.put_fail=0;assert(!i9100_audio_domain_release(&d));assert(!m.temp_owned&&!d.owned);
 }
 setup();memset(&d,0,sizeof(d));assert(!i9100_audio_domain_init(&d,&io));assert(!i9100_audio_domain_enable(&d,1));m.regs[0x3cc0/4]|=3U;
 assert(i9100_audio_domain_acquire(&d)==I9100_AP_BUSY&&!d.owned&&!m.writes&&!m.temp_gets);
 setup();memset(&d,0,sizeof(d));assert(!i9100_audio_domain_init(&d,&io));assert(!i9100_audio_domain_enable(&d,1));m.stall=1;m.regs[0x3cc4/4]|=7;
 assert(i9100_audio_domain_acquire(&d)==I9100_AP_BUSY&&!d.owned&&!m.writes&&!m.temp_gets);
}
int main(void)
{mclk();domain();puts("PASS PMU XUSBXTI9/default OFF/all selectors/10ms/holds/restore; MAUDIO3cc0/3cc4/idle/temporary clocks/1ms/bus confirmation/partial failures/explicit cleanup");return 0;}
