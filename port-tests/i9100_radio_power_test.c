/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_radio_power.h"
struct model {
 uint32_t regs[3][1024],gpio_locked,control_locked,writes,reads,locks,fail_lock,fail_write;
 uint32_t supply_owned,get_calls,put_calls,verify_calls,get_partial,put_fail,verify_fail_at;
 uint32_t idle[2],freeze_time,delay_calls,fail_delay,bt_reset_high;
 uint64_t ns,bt_enable_time;
};
static struct model m;
static struct i9100_gpio_broker gpio;
static struct i9100_radio_power power;
static int gpio_lock(void *cookie){struct model *p=cookie;if(++p->locks==p->fail_lock||p->gpio_locked)return -1;p->gpio_locked=1;return 0;}
static void gpio_unlock(void *cookie){struct model *p=cookie;assert(p->gpio_locked);p->gpio_locked=0;}
static uint32_t read32(void *cookie,unsigned region,unsigned off)
{struct model *p=cookie;assert(p->gpio_locked&&region<3U&&off<4096U);p->reads++;return p->regs[region][off/4U];}
static void write32(void *cookie,unsigned region,unsigned off,uint32_t value)
{
 struct model *p=cookie;assert(p->gpio_locked&&region<3U&&off<4096U);p->writes++;
 if(p->writes==p->fail_write)return;
 uint32_t old=p->regs[region][off/4U];
 if(region==1U&&off==0xc4U&&!(old&16U)&&(value&16U))p->bt_enable_time=p->ns;
 if(region==1U&&off==0xe4U&&!(old&1U)&&(value&1U)){
  assert(p->regs[1][0xc4U/4U]&16U);assert(p->regs[1][0xc64U/4U]&2U);
  assert(p->ns>=p->bt_enable_time+20000000ULL);p->bt_reset_high++;
 }
 p->regs[region][off/4U]=value;
}
static int lock(void *cookie){struct model *p=cookie;if(p->control_locked)return -1;p->control_locked=1;return 0;}
static void unlock(void *cookie){struct model *p=cookie;assert(p->control_locked);p->control_locked=0;}
static uint64_t now(void *cookie){return ((struct model *)cookie)->ns;}
static int delay(void *cookie,uint32_t us)
{struct model *p=cookie;assert(p->control_locked);if(++p->delay_calls==p->fail_delay)return -1;if(!p->freeze_time)p->ns+=(uint64_t)us*1000U;return 0;}
static int idle(void *cookie,enum i9100_radio_role role)
{struct model *p=cookie;assert(p->control_locked);return p->idle[(unsigned)role-1U]?-1:0;}
static void lows(void)
{
 assert(!(m.regs[1][0xc4U/4U]&16U));assert(!(m.regs[1][0xe4U/4U]&5U));assert(!(m.regs[1][0xc64U/4U]&2U));
}
static int get(void *cookie,unsigned *owned)
{struct model *p=cookie;assert(p->control_locked);lows();p->get_calls++;p->supply_owned=1;*owned=1;return p->get_partial?-1:0;}
static int verify(void *cookie)
{struct model *p=cookie;assert(p->control_locked&&p->supply_owned);return ++p->verify_calls==p->verify_fail_at?-1:0;}
static int put(void *cookie)
{struct model *p=cookie;assert(p->control_locked&&!p->idle[0]&&!p->idle[1]);lows();p->put_calls++;if(p->put_fail)return -1;p->supply_owned=0;return 0;}
static struct i9100_radio_io io={&m,lock,unlock,now,delay,idle,get,verify,put};
static void setup(void)
{
 memset(&m,0,sizeof(m));memset(&gpio,0,sizeof(gpio));memset(&power,0,sizeof(power));
 const unsigned offsets[3]={0xc0,0xe0,0xc60},masks[3]={16,5,2};
 for(unsigned n=0;n<3U;n++){
  uint32_t mux=0;for(unsigned pin=0;pin<8U;pin++)if(masks[n]&(1U<<pin))mux|=15U<<(pin*4U);
  m.regs[1][offsets[n]/4U]=0xa5a5a5a5U&~mux;
  m.regs[1][(offsets[n]+4U)/4U]=0x96dbffeeU&~masks[n];
  m.regs[1][(offsets[n]+8U)/4U]=0xccccccccU;m.regs[1][(offsets[n]+12U)/4U]=0x3f5f7f9fU;
 }
 struct i9100_gpio_io gio={&m,read32,write32,gpio_lock,gpio_unlock};
 assert(!i9100_gpio_broker_init(&gpio,&gio,i9100_u1_banks,I9100_BANK_COUNT));assert(!i9100_gpio_broker_enable(&gpio,1));
}
static void open_power(void){assert(!i9100_radio_power_init(&power,1,&gpio,&io,1));}
static void close_power(void)
{assert(!i9100_radio_power_close(&power));lows();assert(!power.enabled&&!m.supply_owned&&!power.supply_owned);assert(!i9100_gpio_broker_enable(&gpio,0));}
static void lifecycle(void)
{
 setup();unsigned before=m.writes;assert(i9100_radio_power_init(&power,0,&gpio,&io,1)==I9100_RADIO_DISABLED&&m.writes==before&&!m.get_calls);
 assert(i9100_radio_power_init(&power,1,&gpio,&io,0)==I9100_RADIO_ARGUMENT&&m.writes==before);
 open_power();assert(power.ready&&power.supply_owned&&!power.powered[0]&&!power.powered[1]);
 assert(!i9100_radio_set_power(&power,I9100_RADIO_WLAN,1)&&power.generation[0]==1U);
 assert(!i9100_radio_set_power(&power,I9100_RADIO_BT,1)&&power.generation[1]==1U&&m.bt_reset_high==1U);
 assert(m.ns==70200000ULL);assert(m.regs[1][0xe4U/4U]&4U);
 struct i9100_radio_hold wlan={0},bt={0};
 assert(!i9100_radio_hold_get(&power,I9100_RADIO_WLAN,&wlan));assert(!i9100_radio_hold_get(&power,I9100_RADIO_BT,&bt));
 before=m.writes;assert(i9100_radio_set_power(&power,I9100_RADIO_WLAN,0)==I9100_RADIO_BUSY&&m.writes==before);
 struct i9100_radio_hold copy=bt;assert(i9100_radio_hold_put(&copy)==I9100_RADIO_OWNER);
 assert(!i9100_radio_hold_put(&wlan));assert(!i9100_radio_set_power(&power,I9100_RADIO_WLAN,0));
 assert(m.regs[1][0xc4U/4U]&16U);assert(m.regs[1][0xe4U/4U]&1U);assert(!(m.regs[1][0xe4U/4U]&4U));
 assert(!i9100_radio_hold_put(&bt));assert(!i9100_radio_set_power(&power,I9100_RADIO_BT,0));
 assert(!i9100_radio_set_power(&power,I9100_RADIO_WLAN,1)&&power.generation[0]==2U);
 assert(!i9100_radio_hold_get(&power,I9100_RADIO_WLAN,&wlan));before=m.writes;
 assert(i9100_radio_power_close(&power)==I9100_RADIO_BUSY&&power.closing&&m.writes==before);
 assert(i9100_radio_set_power(&power,I9100_RADIO_BT,1)==I9100_RADIO_DISABLED);
 assert(!i9100_radio_hold_put(&wlan));close_power();
 const unsigned offsets[3]={0xc0,0xe0,0xc60},masks[3]={16,5,2};
 for(unsigned n=0;n<3U;n++){
  uint32_t mux=0,pull=0;for(unsigned pin=0;pin<8U;pin++)if(masks[n]&(1U<<pin)){mux|=15U<<(pin*4U);pull|=3U<<(pin*2U);}
  assert((m.regs[1][offsets[n]/4U]&~mux)==(0xa5a5a5a5U&~mux));
  assert((m.regs[1][(offsets[n]+4U)/4U]&~masks[n])==(0x96dbffeeU&~masks[n]));
  assert((m.regs[1][(offsets[n]+8U)/4U]&~pull)==(0xccccccccU&~pull));
  assert(m.regs[1][(offsets[n]+12U)/4U]==0x3f5f7f9fU);
 }
}
static void guards(void)
{
 setup();m.regs[1][0xe4U/4U]|=4U;uint32_t snapshot[3][1024];memcpy(snapshot,m.regs,sizeof(snapshot));
 assert(i9100_radio_power_init(&power,1,&gpio,&io,1)==I9100_RADIO_NOT_READY&&!m.writes&&!m.get_calls&&!memcmp(snapshot,m.regs,sizeof(snapshot)));
 assert(!i9100_radio_power_close(&power));assert(!i9100_gpio_broker_enable(&gpio,0));
 setup();m.regs[1][0xe0U/4U]|=2U;assert(i9100_radio_power_init(&power,1,&gpio,&io,1)==I9100_RADIO_NOT_READY&&!m.writes&&!m.get_calls);
 setup();gpio.banks[I9100_GPL1].config.offset=0xa0;
 assert(i9100_radio_power_init(&power,1,&gpio,&io,1)==I9100_RADIO_ARGUMENT&&!m.writes);
 setup();open_power();m.idle[0]=1;unsigned before=m.writes;
 assert(i9100_radio_set_power(&power,I9100_RADIO_WLAN,1)==I9100_RADIO_BUSY&&m.writes==before);m.idle[0]=0;
 power.generation[0]=UINT32_MAX;assert(i9100_radio_set_power(&power,I9100_RADIO_WLAN,1)==I9100_RADIO_BUSY&&m.writes==before);close_power();
 setup();open_power();struct i9100_radio_power copy=power;
 assert(i9100_radio_set_power(&copy,I9100_RADIO_BT,1)==I9100_RADIO_ARGUMENT);close_power();
}
static void faults(void)
{
 setup();open_power();unsigned count=m.writes;close_power();
 for(unsigned n=1;n<=count;n++){
  setup();m.fail_write=n;int e=i9100_radio_power_init(&power,1,&gpio,&io,1);
  assert(!e||e==I9100_RADIO_IO);close_power();
 }
 setup();m.get_partial=1;assert(i9100_radio_power_init(&power,1,&gpio,&io,1)==I9100_RADIO_IO&&m.put_calls==1U&&!m.supply_owned);close_power();
 setup();m.verify_fail_at=1;assert(i9100_radio_power_init(&power,1,&gpio,&io,1)==I9100_RADIO_NOT_READY&&!m.supply_owned);close_power();
 for(unsigned role=1;role<=2U;role++){
  setup();open_power();unsigned before=m.writes;assert(!i9100_radio_set_power(&power,(enum i9100_radio_role)role,1));count=m.writes-before;close_power();
  for(unsigned n=1;n<=count;n++){
   setup();open_power();m.fail_write=m.writes+n;int e=i9100_radio_set_power(&power,(enum i9100_radio_role)role,1);
   assert(!e||e==I9100_RADIO_IO);if(e)assert(!power.generation[role-1U]&&!power.powered[role-1U]);close_power();
  }
 }
 setup();open_power();m.freeze_time=1;
 assert(i9100_radio_set_power(&power,I9100_RADIO_BT,1)==I9100_RADIO_TIMEOUT&&!m.bt_reset_high&&!power.powered[1]);m.freeze_time=0;close_power();
 setup();open_power();m.fail_delay=2;
 assert(i9100_radio_set_power(&power,I9100_RADIO_BT,1)==I9100_RADIO_IO&&!power.powered[1]);close_power();
 setup();open_power();m.verify_fail_at=3;
 assert(i9100_radio_set_power(&power,I9100_RADIO_WLAN,1)==I9100_RADIO_NOT_READY&&!power.powered[0]);close_power();
 setup();open_power();m.put_fail=1;
 assert(i9100_radio_power_close(&power)==I9100_RADIO_IO&&power.supply_owned&&power.pins[0].active);m.put_fail=0;close_power();
 /* The final three GPIO locks in close release KEEP leases. Fail the first,
  * let the other banks release, then check retry doesn't touch those banks. */
 setup();open_power();unsigned start=m.locks;assert(!i9100_radio_power_close(&power));unsigned closing_locks=m.locks-start;
 setup();open_power();m.fail_lock=m.locks+closing_locks-2U;
 assert(i9100_radio_power_close(&power)==I9100_RADIO_IO&&!power.configured&&!power.supply_owned);
 unsigned writes=m.writes;m.fail_lock=0;assert(!i9100_radio_power_close(&power)&&m.writes==writes);close_power();
}
int main(void)
{lifecycle();guards();faults();puts("PASS U1 low-before-supply/BT20+50ms/WLAN200us/independent roles/generation holds/foreign fields/AF+high rejection/all GPIO writes/clock and delay faults/supply owner/partial KEEP cleanup; no real GPIO or LPO proof");return 0;}
