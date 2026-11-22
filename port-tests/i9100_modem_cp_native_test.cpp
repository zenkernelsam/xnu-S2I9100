/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_modem_cp_native.h"
#include "i9100_native_parent_model.h"
extern "C" int __real_i9100_gpio_output(struct i9100_gpio_lease *,unsigned,unsigned);
static int output_fail;
extern "C" int __wrap_i9100_gpio_output(struct i9100_gpio_lease *l,unsigned pin,unsigned value)
{return output_fail?I9100_GPIO_IO:__real_i9100_gpio_output(l,pin,value);}
extern "C" int __real_i9100_gpio_release(struct i9100_gpio_lease *,enum i9100_gpio_release_policy);
static int release_fail_bank=-1;
extern "C" int __wrap_i9100_gpio_release(struct i9100_gpio_lease *l,enum i9100_gpio_release_policy p)
{return (int)l->bank==release_fail_bank?I9100_GPIO_BUSY:__real_i9100_gpio_release(l,p);}
static uint32_t reg(unsigned region_base,unsigned off){return memory[region_base][off/4U];}
#define GPC1DAT 0x84U
#define GPX1CON 0xc20U
#define GPX1DAT 0xc24U
#define GPX1PUD 0xc28U
#define GPY4CON 0x1a0U
#define GPY4DAT 0x1a4U
static void preset_cold()
{
 /* cold board: all our pins input/low except hostwake idle high; neighbors
  * carry non-zero AF/pull patterns so their cells prove untouched. */
 memory[0x11000000U][GPY4CON/4U]=0xa5a5a5a5U;
 memory[0x11000000U][GPY4DAT/4U]=0x9696dbdbU;
 memory[0x11000000U][(GPY4CON+8U)/4U]=0xccccccccU;
 memory[0x11000000U][(GPY4CON+12U)/4U]=0x3f5f7f9fU;
 memory[0x11000000U][GPX1CON/4U]=0xa5a5a5a5U;
 memory[0x11000000U][GPX1DAT/4U]=0x9696dbdbU;
 memory[0x11000000U][GPX1PUD/4U]=0xccccccccU;
 memory[0x11000000U][(GPX1CON+12U)/4U]=0x3f5f7f9fU;
 memory[0x11400000U][0x80U/4U]=0xa5a5a5a5U;
 memory[0x11400000U][0x84U/4U]=0x9696dbdbU;
 memory[0x11400000U][0x88U/4U]=0xccccccccU;
 memory[0x11400000U][0x8cU/4U]=0x3f5f7f9fU;
 static const unsigned our_gpc1=1U<<1,our_gpx1=(1U<<0)|(1U<<1)|(1U<<2)|(1U<<3)|(1U<<4)|(1U<<6),
  our_gpy4=(1U<<2)|(1U<<6);
 for(unsigned pin=0;pin<8U;pin++){
  if(our_gpc1&(1U<<pin)){memory[0x11400000U][0x80U/4U]&=~(15U<<(pin*4U));memory[0x11400000U][0x84U/4U]&=~(1U<<pin);}
  if(our_gpx1&(1U<<pin)){memory[0x11000000U][GPX1CON/4U]&=~(15U<<(pin*4U));memory[0x11000000U][GPX1DAT/4U]&=~(1U<<pin);}
  if(our_gpy4&(1U<<pin)){memory[0x11000000U][GPY4CON/4U]&=~(15U<<(pin*4U));memory[0x11000000U][GPY4DAT/4U]&=~(1U<<pin);}
 }
 memory[0x11000000U][GPX1DAT/4U]|=2U; /* hostwake idle high */
}
static i9100_peripheral_native *setup_parent()
{setup();i9100_peripheral_native *c=parent();preset_cold();return c;}
static i9100_modem_cp_native *open(i9100_peripheral_native *c)
{i9100_modem_cp_native *n=0;assert(!i9100_modem_cp_native_open(1,c,1,&n)&&n);return n;}
static void drive(i9100_modem_cp_native *n,uint64_t us)
{
 int e=i9100_modem_cp_native_tick(n);
 for(unsigned k=0;k<1100U&&e==I9100_CP_BUSY;k++){ns+=us*1000U;e=i9100_modem_cp_native_tick(n);}
 assert(!e);
}
static void close_ok(i9100_peripheral_native *&c,i9100_modem_cp_native *&n)
{
 assert(!i9100_modem_cp_native_close(&n)&&!n);
 assert(!(reg(0x11400000U,GPC1DAT)&2U)&&!(reg(0x11000000U,GPX1DAT)&0x11U)&&
        !(reg(0x11000000U,GPY4DAT)&0x44U)); /* outputs only; inputs keep CP levels */
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void open_close()
{
 i9100_peripheral_native *c=setup_parent();unsigned before=allocations;
 i9100_modem_cp_native *n=0;
 assert(i9100_modem_cp_native_open(0,c,1,&n)==I9100_CP_DISABLED&&!n&&allocations==before);
 assert(i9100_modem_cp_native_open(1,0,1,&n)==I9100_CP_ARGUMENT&&!n);
 assert(i9100_modem_cp_native_open(1,c,0,&n)==I9100_CP_ARGUMENT&&!n);
 n=open(c);
 struct i9100_modem_cp_status s;
 assert(!i9100_modem_cp_native_status(n,&s)&&s.ready&&s.state==I9100_CP_OFF&&s.pins_owned==3U);
 i9100_modem_cp_native *second=0;
 assert(i9100_modem_cp_native_open(1,c,1,&second)==I9100_CP_BUSY&&!second); /* single controller */
 i9100_modem_cp_native *old=n;
 assert(i9100_modem_cp_native_open(1,c,1,&n)==I9100_CP_BUSY&&n==old);
 close_ok(c,n);
 c=setup_parent();n=open(c);close_ok(c,n); /* reservation released: reopen works */
}
static void sequence()
{
 i9100_peripheral_native *c=setup_parent();i9100_modem_cp_native *n=open(c);
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_ON)==I9100_CP_BUSY);
 struct i9100_modem_cp_status s;
 assert(!i9100_modem_cp_native_status(n,&s)&&s.op==I9100_CP_OP_ON);
 ns+=500000000ULL;drive(n,20000U);
 assert(!i9100_modem_cp_native_status(n,&s)&&s.state==I9100_CP_BOOTING&&!s.op);
 assert((reg(0x11000000U,GPX1DAT)&0x10U)&&(reg(0x11000000U,GPY4DAT)&0x44U)&&
        !(reg(0x11400000U,GPC1DAT)&2U));
 struct i9100_modem_cp_pins p;assert(!i9100_modem_cp_native_poll(n,&p));
 assert(p.pda_active==1U&&p.req_reset==1U&&p.host_wake==1U&&p.seq);
 memory[0x11000000U][GPX1DAT/4U]|=1U<<6U; /* CP reports phone_active */
 int pe=i9100_modem_cp_native_poll(n,&p);
 assert(!pe&&p.phone_active==1U&&p.seq>=2U);
 /* wake: hostwake high, swk low -> pulse + poll; CP drops hostwake at poll2 */
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_WAKE)==I9100_CP_BUSY);
 assert((reg(0x11000000U,GPX1DAT)&1U)==1U);
 ns+=5000000ULL;assert(i9100_modem_cp_native_tick(n)==I9100_CP_BUSY);
 ns+=5000000ULL;memory[0x11000000U][GPX1DAT/4U]&=~2U;
 assert(i9100_modem_cp_native_tick(n)==0);
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_ON)==I9100_CP_STATE); /* already booting */
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_RESET)==I9100_CP_BUSY);
 ns+=20000000ULL;drive(n,20000U);
 assert(!i9100_modem_cp_native_status(n,&s)&&s.state==I9100_CP_BOOTING);
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_OFF)==0);
 assert(!i9100_modem_cp_native_status(n,&s)&&s.state==I9100_CP_OFF);
 close_ok(c,n);
}
static void cancel_and_close()
{
 i9100_peripheral_native *c=setup_parent();i9100_modem_cp_native *n=open(c);
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_ON)==I9100_CP_BUSY);
 assert(i9100_modem_cp_native_cancel(n)==0);
 struct i9100_modem_cp_status s;
 assert(!i9100_modem_cp_native_status(n,&s)&&s.state==I9100_CP_OFF&&!s.op&&s.cancels==1U);
 assert(!(reg(0x11000000U,GPX1DAT)&0x10U)); /* safe-off re-asserted CP_RESET */
 assert(i9100_modem_cp_native_tick(n)==0);
 /* close while an op is pending aborts it through the owned safe-off */
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_ON)==I9100_CP_BUSY);
 close_ok(c,n);
}
static void retained_and_foreign()
{
 i9100_peripheral_native *c=setup_parent();
 /* foreign broker owner on one pin blocks open without any CP writes */
 struct i9100_gpio_lease foreign={};
 assert(!i9100_gpio_claim(i9100_peripheral_native_gpio(c),I9100_GPX1,1U<<0,&foreign));
 i9100_modem_cp_native *n=0;
 assert(i9100_modem_cp_native_open(1,c,1,&n)==I9100_CP_BUSY&&!n);
 assert(!i9100_gpio_release(&foreign,I9100_GPIO_RESTORE));
 /* cold-state reject through the real parent */
 memory[0x11000000U][GPX1DAT/4U]|=1U<<6U; /* phone_active already high */
 assert(i9100_modem_cp_native_open(1,c,1,&n)==I9100_CP_STATE&&!n);
 memory[0x11000000U][GPX1DAT/4U]&=~(1U<<6U);
 unsigned gen0=i9100_peripheral_native_gpio(c)->generation; /* next claim gets gen0+1..+3 */
 n=open(c);
 /* broker-side owner revocation is caught by native qualification */
 c->gpio.banks[I9100_GPC1].owners[1]=0;
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_ON)==I9100_CP_OWNER);
 assert(i9100_modem_cp_native_tick(n)==0); /* no op armed */
 c->gpio.banks[I9100_GPC1].owners[1]=gen0+1U;
 /* failed close retains everything; same-owner retry succeeds */
 assert(i9100_modem_cp_native_start(n,I9100_CP_OP_ON)==I9100_CP_BUSY);
 c->gpio.banks[I9100_GPY4].owners[2]=0;
 assert(i9100_modem_cp_native_close(&n)==I9100_CP_OWNER&&n);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_native_status(n,&s)&&s.pins_owned==3U);
 c->gpio.banks[I9100_GPY4].owners[2]=gen0+3U;
 close_ok(c,n);
}
static void failed_open_retains()
{
 /* Probe F1: a mid-init output failure must hand the caller a live object;
  * open disposes nothing while the core still holds leases/config. */
 i9100_peripheral_native *c=setup_parent();
 i9100_modem_cp_native *n=0;
 output_fail=1;
 assert(i9100_modem_cp_native_open(1,c,1,&n)==I9100_CP_IO&&n);
 struct i9100_modem_cp_status s;
 assert(!i9100_modem_cp_native_status(n,&s)&&s.fault&&s.error==I9100_CP_IO&&s.pins_owned==3U);
 output_fail=0;
 /* same-owner close completes cleanup; parent and reservation are released */
 close_ok(c,n);
}
static void cold_reject_rollback()
{
 /* Probe F1 native side: cold reject + BUSY rollback leaves an
  * un-configured lease; close must finish RESTORE before freeing. */
 i9100_peripheral_native *c=setup_parent();
 memory[0x11000000U][GPX1DAT/4U]|=1U<<6U; /* phone_active: cold reject */
 release_fail_bank=I9100_GPX1;
 i9100_modem_cp_native *n=0;
 assert(i9100_modem_cp_native_open(1,c,1,&n)==I9100_CP_IO&&n);
 struct i9100_modem_cp_status s;
 assert(!i9100_modem_cp_native_status(n,&s)&&s.pins_owned==1U&&!s.configured&&!s.enabled);
 /* close while the bank is still BUSY keeps the object + parent hold */
 assert(i9100_modem_cp_native_close(&n)==I9100_CP_IO&&n);
 release_fail_bank=-1;
 /* retry: RESTORE finishes, close only frees once nothing is held */
 assert(!i9100_modem_cp_native_close(&n)&&!n);
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
int main()
{
 open_close();sequence();cancel_and_close();retained_and_foreign();failed_open_retains();
 cold_reject_rollback();
 puts("PASS native CP adapter: real parent hold+GPIO broker, single controller, "
      "cold/foreign/owner qualification, cancel+close retention, poll seq; "
      "model registers only, no IRQ/EHCI/PHY/CP evidence");
 return 0;
}
