/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_modem_cp.h"
#include "i9100_peripheral_board.h"
#include "i9100_modem_cp_facade.h"
static struct i9100_cp_gpio_model m;
static struct i9100_gpio_broker gpio;
static struct i9100_modem_cp cp;
static struct i9100_gpio_io gio={&m,cp_gpio_read,cp_gpio_write,cp_gpio_lock,cp_gpio_unlock};
static struct i9100_modem_cp_io io={&m,cp_lock,cp_unlock,cp_now,cp_delay};
#define GPC1_CP_ON 1U
#define GPX1_SWK 0U
#define GPX1_HWK 1U
#define GPX1_DUMP 2U
#define GPX1_SUSPEND 3U
#define GPX1_RST 4U
#define GPX1_PHONE 6U
#define GPY4_PDA 2U
#define GPY4_REQ 6U
static void setup(void)
{
 memset(&m,0,sizeof(m));memset(&gpio,0,sizeof(gpio));memset(&cp,0,sizeof(cp));
 i9100_cp_gpio_cold(&m);
 assert(!i9100_gpio_broker_init(&gpio,&gio,i9100_u1_banks,I9100_BANK_COUNT));
 assert(!i9100_gpio_broker_enable(&gpio,1));
}
static void open_cp(void){assert(!i9100_modem_cp_init(&cp,1,&gpio,&io,1));}
static void close_cp(void)
{
 assert(!i9100_modem_cp_close(&cp));
 assert(!i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)&&!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST)&&
        !i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK)&&!i9100_cp_level(&m,CP_SLOT_GPY4,GPY4_PDA)&&
        !i9100_cp_level(&m,CP_SLOT_GPY4,GPY4_REQ));
 assert(!i9100_gpio_broker_enable(&gpio,0));
}
static void advance(uint64_t us){m.ns+=us*1000U;}
static void lifecycle(void)
{
 setup();unsigned before=m.writes;
 assert(i9100_modem_cp_init(&cp,0,&gpio,&io,1)==I9100_CP_DISABLED&&m.writes==before);
 memset(&cp,0,sizeof(cp));
 assert(i9100_modem_cp_init(&cp,1,&gpio,&io,0)==I9100_CP_ARGUMENT&&m.writes==before);
 memset(&cp,0,sizeof(cp));
 open_cp();
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.ready&&s.state==I9100_CP_OFF&&s.pins_owned==3U&&!s.fault&&!s.op);
 struct i9100_modem_cp_pins p;assert(!i9100_modem_cp_poll(&cp,&p));
 assert(p.seq==1U&&!p.cp_on&&!p.cp_reset&&!p.pda_active&&!p.req_reset&&!p.slave_wake&&
        !p.phone_active&&p.host_wake==1U&&!p.dump_int&&!p.suspend_req);
 assert(i9100_modem_cp_tick(&cp)==0); /* no op: idle tick */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_RESET)==I9100_CP_STATE);
 assert(i9100_modem_cp_start(&cp,(enum i9100_cp_op)9)==I9100_CP_ARGUMENT);
 close_cp();assert(i9100_modem_cp_poll(&cp,&p)==I9100_CP_DISABLED);
 assert(i9100_modem_cp_close(&cp)==0);
}
static void power_on(void)
{
 setup();open_cp();
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 assert(i9100_modem_cp_status(&cp,&(struct i9100_modem_cp_status){0})==0);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.op==I9100_CP_OP_ON&&s.step==2U&&m.ns==160000ULL);
 /* vendor order so far: CP_ON/CP_RESET low @0us, PDA low, 500ms pending */
 assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY); /* stalled tick is honest */
 advance(500000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY); /* now at 20ms wait */
 advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.state==I9100_CP_BOOTING&&!s.op);
 /* transition times: rst@500160us req@+160us on@+160us on-low@+60us pda@+20ms */
 assert(i9100_cp_pin_at(&m,CP_SLOT_GPX1,GPX1_RST)==500160000ULL);
 assert(i9100_cp_pin_at(&m,CP_SLOT_GPY4,GPY4_REQ)==500320000ULL);
 assert(i9100_cp_pin_at(&m,CP_SLOT_GPC1,GPC1_CP_ON)==500540000ULL);
 assert(i9100_cp_pin_at(&m,CP_SLOT_GPY4,GPY4_PDA)==520540000ULL);
 assert(i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)==0U&&i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST)==1U&&
        i9100_cp_level(&m,CP_SLOT_GPY4,GPY4_REQ)==1U&&i9100_cp_level(&m,CP_SLOT_GPY4,GPY4_PDA)==1U);
 /* PHONE_ACTIVE high still only means BOOTING in the vendor handler */
 m.regs[1][(0xc20U+4U)/4U]|=1U<<6U;
 struct i9100_modem_cp_pins p;assert(!i9100_modem_cp_poll(&cp,&p)&&p.phone_active==1U);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.state==I9100_CP_BOOTING);
 /* OFF */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_OFF)==0);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.state==I9100_CP_OFF);
 assert(!i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)&&!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST));
 /* RESET from OFF is rejected */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_RESET)==I9100_CP_STATE);
 close_cp();
}
static void reset_seq(void)
{
 setup();open_cp();assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);i9100_modem_cp_tick(&cp);advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 uint64_t base=m.ns;unsigned writes0=m.pin_writes;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_RESET)==I9100_CP_BUSY);
 /* reset drops CP_RESET+REQ low immediately, waits 20ms offline */
 assert(!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST)&&!i9100_cp_level(&m,CP_SLOT_GPY4,GPY4_REQ));
 advance(20000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY);
 advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s)&&s.state==I9100_CP_BOOTING);
 assert(i9100_cp_pin_at(&m,CP_SLOT_GPX1,GPX1_RST)==base+20000000ULL);
 assert(i9100_cp_pin_at(&m,CP_SLOT_GPY4,GPY4_REQ)==base+20160000ULL);
 assert(i9100_cp_pin_at(&m,CP_SLOT_GPC1,GPC1_CP_ON)==base+20320000ULL);
 assert(m.pin_writes>writes0);close_cp();
}
static void wake_seq(void)
{
 setup();open_cp();assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);i9100_modem_cp_tick(&cp);advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 /* hostwake inactive(high), slavewake low: pulse high + poll */
 unsigned w0=m.writes;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_WAKE)==I9100_CP_BUSY);
 assert(i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK)==1U);
 advance(5000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY); /* poll1 miss */
 advance(5000U);m.regs[1][(0xc20U+4U)/4U]&=~2U; /* CP drops hostwake low */
 assert(i9100_modem_cp_tick(&cp)==0);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s)&&!s.op&&s.state==I9100_CP_BOOTING);
 /* re-wake while swk still high: low 5ms -> high 5ms -> poll (vendor order) */
 m.regs[1][(0xc20U+4U)/4U]|=2U;w0=m.writes;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_WAKE)==I9100_CP_BUSY);
 assert(i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK)==0U);
 advance(5000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY);
 assert(i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK)==1U);
 advance(5000U);m.regs[1][(0xc20U+4U)/4U]&=~2U;assert(i9100_modem_cp_tick(&cp)==0);
 /* already active hostwake: zero writes, immediate done */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_WAKE)==0&&m.writes==w0+4U);
 close_cp();
}
static void wake_timeout(void)
{
 setup();open_cp();assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);i9100_modem_cp_tick(&cp);advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_WAKE)==I9100_CP_BUSY);
 for(unsigned n=0;n<19U;n++){advance(5000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY);}
 advance(5000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_TIMEOUT); /* 20 polls = 100ms bound */
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.fault&&s.error==I9100_CP_TIMEOUT&&s.state==I9100_CP_FAILED&&s.pins_owned==3U);
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_OFF)==I9100_CP_STATE); /* faulted: only close */
 close_cp();
}
static void cancel_windows(void)
{
 setup();open_cp();
 /* cancel during first long wait */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 unsigned w0=m.writes;
 assert(i9100_modem_cp_cancel(&cp)==0);
 assert(m.writes>=w0+2U); /* owned safe-off appended */
 assert(!i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)&&!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST)&&
        !i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK));
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.state==I9100_CP_OFF&&!s.op&&s.cancels==1U&&!s.fault);
 assert(i9100_modem_cp_tick(&cp)==0); /* cancelled: nothing left to run */
 advance(500000U);assert(i9100_modem_cp_tick(&cp)==0); /* late tick: still quiet */
 assert(i9100_modem_cp_cancel(&cp)==0); /* repeat cancel is idempotent */
 /* cancel during the 20ms tail */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY);
 assert(i9100_modem_cp_cancel(&cp)==0);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.state==I9100_CP_OFF&&s.cancels==2U);
 assert(!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST)); /* reset re-asserted */
 /* cancel mid-wake */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);i9100_modem_cp_tick(&cp);advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_WAKE)==I9100_CP_BUSY);
 assert(i9100_modem_cp_cancel(&cp)==0&&!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK));
 close_cp();
}
static void fault_injection(void)
{
 setup();open_cp();
 /* drop each level-changing DAT write mid-sequence -> readback IO fault,
  * retained (no-change writes are honestly invisible to a dropped store) */
 static const unsigned changed[]={7U,9U,11U,13U,15U};
 for(unsigned n=0;n<5U;n++){
  setup();open_cp();m.drop_write=m.writes+changed[n];
  int e=i9100_modem_cp_start(&cp,I9100_CP_OP_ON);
  if(!e||e==I9100_CP_BUSY){advance(520000U);for(unsigned k=0;k<8U&&cp.op;k++){e=i9100_modem_cp_tick(&cp);if(cp.op&&cp.pending)advance(30000U);}}
  struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
  assert(s.fault&&s.error!=0&&s.pins_owned==3U);
  assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_STATE);
  close_cp();
 }
 /* inline-delay failure (160us wait never elapses) */
 setup();open_cp();m.freeze_time=1;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_TIMEOUT);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s)&&s.fault&&s.pins_owned==3U);
 m.freeze_time=0;close_cp();
 /* pending-wait stall bound: frozen clock, repeated ticks */
 setup();open_cp();assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 for(unsigned n=0;n<1024U;n++)assert(i9100_modem_cp_tick(&cp)==I9100_CP_BUSY);
 assert(i9100_modem_cp_tick(&cp)==I9100_CP_TIMEOUT);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.fault&&s.state==I9100_CP_FAILED);
 close_cp();
 /* regressing clock at a pending wait */
 setup();open_cp();assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 m.ns-=100000ULL;assert(i9100_modem_cp_tick(&cp)==I9100_CP_TIMEOUT);
 close_cp();
}
static void cold_reject(void)
{
 static const struct {unsigned slot,pin,con,dat;}cases[]={
  {CP_SLOT_GPX1,GPX1_PHONE,0U,1U}, /* CP already signalling boot/active */
  {CP_SLOT_GPC1,GPC1_CP_ON,1U,1U}, /* CP_ON already driven high */
  {CP_SLOT_GPX1,GPX1_HWK,0U,0U},   /* hostwake asserted = running/suspended CP */
  {CP_SLOT_GPX1,GPX1_DUMP,0U,1U},  /* CP dump signalled */
  {CP_SLOT_GPX1,GPX1_SUSPEND,0U,1U},
  {CP_SLOT_GPY4,GPY4_REQ,0U,1U},   /* reset-request released */
  {CP_SLOT_GPX1,GPX1_PHONE,15U,0U} /* inherited EINT/peripheral mux */
 };
 for(unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);n++){
  setup();
  uint32_t snapshot[3][4];
  for(unsigned s=0;s<CP_SLOTS;s++)for(unsigned r=0;r<4U;r++)
   snapshot[s][r]=m.regs[i9100_cp_slot_map[s].region][(i9100_cp_slot_map[s].base+4U*r)/4U];
  unsigned base=i9100_cp_slot_map[cases[n].slot].base,region=i9100_cp_slot_map[cases[n].slot].region;
  m.regs[region][base/4U]&=~(15U<<(cases[n].pin*4U));
  m.regs[region][base/4U]|=cases[n].con<<(cases[n].pin*4U);
  if(cases[n].dat)m.regs[region][(base+4U)/4U]|=1U<<cases[n].pin;
  else m.regs[region][(base+4U)/4U]&=~(1U<<cases[n].pin);
  assert(i9100_modem_cp_init(&cp,1,&gpio,&io,1)==I9100_CP_STATE);
  assert(cp.fault&&cp.state==I9100_CP_FAILED);
  /* unmodified partial teardown: our registers byte-identical to preset */
  for(unsigned s=0;s<CP_SLOTS;s++)for(unsigned r=0;r<4U;r++){
   if(s==cases[n].slot&&r==0U){
    unsigned keep=~(15U<<(cases[n].pin*4U));
    assert((m.regs[i9100_cp_slot_map[s].region][(i9100_cp_slot_map[s].base)/4U]&keep)==
           (snapshot[s][r]&keep));
   }else if(s==cases[n].slot&&r==1U)continue;
   else assert(m.regs[i9100_cp_slot_map[s].region][(i9100_cp_slot_map[s].base+4U*r)/4U]==snapshot[s][r]);
  }
  assert(!i9100_gpio_broker_enable(&gpio,0)); /* nothing still claimed */
 }
}
static void partial_claim(void)
{
 setup();struct i9100_gpio_lease foreign={0};
 assert(!i9100_gpio_claim(&gpio,I9100_GPY4,1U<<6U,&foreign)); /* foreign owner of REQ_RST */
 assert(i9100_modem_cp_init(&cp,1,&gpio,&io,1)==I9100_CP_BUSY);
 assert(!cp.gpc1.active&&!cp.gpx1.active&&!cp.gpy4.active);
 unsigned base=i9100_cp_slot_map[CP_SLOT_GPY4].base;
 assert((m.regs[1][base/4U]>>24U&15U)==0U||(m.regs[1][base/4U]>>24U&15U)!=1U); /* pin6 not driven */
 assert(!i9100_gpio_release(&foreign,I9100_GPIO_RESTORE));
 assert(!i9100_gpio_broker_enable(&gpio,0));
}
static void neighbors(void)
{
 setup();
 /* snapshot untouched neighbor cells; then run the full machine */
 uint32_t before[3][4];
 for(unsigned s=0;s<CP_SLOTS;s++)for(unsigned r=0;r<4U;r++)
  before[s][r]=m.regs[i9100_cp_slot_map[s].region][(i9100_cp_slot_map[s].base+4U*r)/4U];
 static const unsigned keep[CP_SLOTS]={0x18U,1U<<5U,(1U<<0)|(1U<<1)|(1U<<3)|(1U<<5)|(1U<<7)};
 open_cp();
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);i9100_modem_cp_tick(&cp);advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_OFF)==0);
 close_cp();
 for(unsigned s=0;s<CP_SLOTS;s++){
  unsigned base=i9100_cp_slot_map[s].base,region=i9100_cp_slot_map[s].region;
  for(unsigned pin=0;pin<8U;pin++)if(keep[s]&(1U<<pin)){
   unsigned nb=(m.regs[region][base/4U]>>(pin*4U))&15U,pb=(before[s][0]>>(pin*4U))&15U;
   assert(nb==pb); /* neighbor mux never touched */
   assert(((m.regs[region][(base+4U)/4U]>>pin)&1U)==((before[s][1]>>pin)&1U)); /* neighbor DAT */
   assert(((m.regs[region][(base+8U)/4U]>>(pin*2U))&3U)==((before[s][2]>>(pin*2U))&3U));
   assert(((m.regs[region][(base+12U)/4U]>>(pin*2U))&3U)==((before[s][3]>>(pin*2U))&3U));
  }
 }
}
static void owner_isolation(void)
{
 setup();open_cp();
 struct i9100_modem_cp copy=cp; /* byte copy must not control hardware */
 assert(i9100_modem_cp_start(&copy,I9100_CP_OP_ON)==I9100_CP_ARGUMENT);
 assert(i9100_modem_cp_tick(&copy)==I9100_CP_ARGUMENT);
 assert(i9100_modem_cp_poll(&copy,&(struct i9100_modem_cp_pins){0})==I9100_CP_ARGUMENT);
 assert(i9100_modem_cp_close(&copy)==I9100_CP_ARGUMENT);
 assert(cp.state==I9100_CP_OFF&&m.writes); /* real object untouched by the copy */
 struct i9100_modem_cp blank={0};
 assert(i9100_modem_cp_start(&blank,I9100_CP_OP_ON)==I9100_CP_ARGUMENT);
 close_cp();
}
static void cleanup_retry(void)
{
 setup();open_cp();
 /* close from BOOTING so safe-off really lowers CP_RESET (visible drop) */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(520000U);for(unsigned k=0;k<8U&&cp.op;k++){i9100_modem_cp_tick(&cp);if(cp.op&&cp.pending)advance(30000U);}
 struct i9100_modem_cp_status st0;assert(!i9100_modem_cp_status(&cp,&st0)&&st0.state==I9100_CP_BOOTING);
 m.drop_write=m.writes+5U; /* CP_RESET DAT 1->0: first changing safe-off write */
 assert(i9100_modem_cp_close(&cp)==I9100_CP_IO);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.fault&&s.pins_owned==3U); /* failure retains resources */
 m.drop_write=0;
 assert(i9100_modem_cp_close(&cp)==0); /* same-owner cleanup succeeds after repair */
 assert(!i9100_gpio_broker_enable(&gpio,0));
}
static void high_pulse_recovery(void)
{
 /* Probe sequence under the R015 contract: the pulse delay failure itself
  * converges CP_ON low; cancel/close then only confirm the owned state. */
 setup();open_cp();m.fail_delay=4U; /* step4: CP_ON high pulse delay */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_IO);
 assert(i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)==0U); /* auto safe-off done */
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.fault&&s.error==I9100_CP_IO&&!s.op&&s.state==I9100_CP_FAILED);
 assert(i9100_modem_cp_cancel(&cp)==0);
 assert(!i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)&&
        !i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST)&&!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK));
 assert(i9100_modem_cp_cancel(&cp)==0); /* repeat cancel: honest idempotent 0 */
 /* cancelled safe-off failure after auto-off retains ownership + first error */
 setup();open_cp();m.fail_delay=4U;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 gpio.banks[I9100_GPY4].owners[2]=0; /* GPY4 stolen: auto-off leaves CP_ON low, bank refused */
 advance(500000U);assert(i9100_modem_cp_tick(&cp)==I9100_CP_IO);
 assert(i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)==0U);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.error==I9100_CP_IO&&
        s.cleanup_error==I9100_CP_OWNER&&s.pins_owned==3U);
 assert(i9100_modem_cp_cancel(&cp)==I9100_CP_OWNER); /* still refused until repaired */
 gpio.banks[I9100_GPY4].owners[2]=cp.gpy4.generation;
 assert(i9100_modem_cp_cancel(&cp)==0);
 assert(!i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)&&!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_SWK));
 close_cp();
}
static void partial_release(void)
{
 /* Probe F2: a temporarily BUSY mid-release left one bank held; the old
  * retry wrote already-returned pins and preflight demanded all-active. */
 setup();open_cp();
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);i9100_modem_cp_tick(&cp);advance(20000U);assert(i9100_modem_cp_tick(&cp)==0);
 /* close does 5 output writes (locks 1-5) then releases gpc1(6)/gpx1(7)/gpy4(8) */
 m.fail_lock=m.locks+7U; /* GPX1 release enter fails */
 assert(i9100_modem_cp_close(&cp)==I9100_CP_IO);
 assert(!cp.gpc1.active&&cp.gpx1.active&&!cp.gpy4.active);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s)&&s.pins_owned==1U);
 /* retry: only the still-held bank is touched/released */
 m.fail_lock=0;unsigned log_at=m.log_n;
 assert(i9100_modem_cp_close(&cp)==0);
 for(unsigned n=log_at;n<m.log_n;n++)
  assert(m.log[n].region==1U&&m.log[n].off>=0xc20U&&m.log[n].off<0xc30U);
 assert(!cp.gpx1.active);
 assert(!i9100_gpio_broker_enable(&gpio,0));
}
static void partial_init_retain(void)
{
 /* Probe F1: mid-init write failure must keep the object cleanable. */
 setup();m.poison_from=8U; /* init configure write 8 onward stores wrong data */
 assert(i9100_modem_cp_init(&cp,1,&gpio,&io,1)==I9100_CP_IO);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.fault&&s.error==I9100_CP_IO&&s.pins_owned==3U&&!s.enabled);
 m.poison_from=0;
 /* enabled==0 must not gate cleanup: same-owner close finishes it */
 assert(i9100_modem_cp_close(&cp)==0);
 assert(!cp.gpc1.active&&!cp.gpx1.active&&!cp.gpy4.active);
 assert(!i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)&&!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST));
 assert(i9100_modem_cp_close(&cp)==0); /* cleaned: zero-resource close, no IO */
 assert(!i9100_gpio_broker_enable(&gpio,0));
}
static void poll_integrity(void)
{
 /* F4: first/mid-read failure publishes nothing and consumes no seq. */
 for(unsigned n=0;n<2U;n++){
  setup();open_cp();
  struct i9100_modem_cp_pins p;memset(&p,0xa5,sizeof(p));
  uint32_t seq0=cp.seq;m.fail_lock=m.locks+(n?6U:2U); /* 1st or 5th pin read */
  assert(i9100_modem_cp_poll(&cp,&p)==I9100_CP_IO);
  assert(p.seq==0xa5a5a5a5U&&p.cp_on==0xa5U&&p.phone_active==0xa5U&&cp.seq==seq0);
  struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s)&&s.fault&&s.error==I9100_CP_IO);
  m.fail_lock=0;close_cp();
 }
}
static void clock_boundaries(void)
{
 /* F4: advancing clock at any tick rate never stalls; frozen/late/overflow do. */
 setup();open_cp();
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 for(unsigned n=0;n<1100U&&cp.op;n++){advance(1000U);i9100_modem_cp_tick(&cp);}
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(!s.fault&&s.state==I9100_CP_BOOTING); /* ~520ms elapsed, high-frequency ticks */
 close_cp();
 /* huge-late resume refuses new op writes after the total budget; the
  * fault path still performs its accurate owned safe-off (low writes) */
 setup();open_cp();assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(4000000U);unsigned log_at=m.log_n;
 assert(i9100_modem_cp_tick(&cp)==I9100_CP_TIMEOUT);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.fault&&s.error==I9100_CP_TIMEOUT);
 for(unsigned n=log_at;n<m.log_n;n++) /* no write ever raised CP_RESET */
  assert(!(m.log[n].region==1U&&m.log[n].off==0xc24U&&(m.log[n].after&0x10U)));
 assert(!i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST));
 close_cp();
 /* arm overflow near UINT64_MAX is refused before any due is booked */
 setup();open_cp();m.ns=UINT64_MAX-200000ULL;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_TIMEOUT);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.fault&&s.error==I9100_CP_TIMEOUT);
 close_cp();
}
static void cold_rollback_case(unsigned fail_lock,struct i9100_gpio_lease *held)
{
 /* Probe F1: cold-state reject rolls claims back via RESTORE; a BUSY
  * mid-rollback must not let close abandon the remaining lease. Each
  * bank's residual gets its own case below. */
 setup();
 m.regs[1][0xc24/4U]|=1U<<6U; /* phone_active already high: cold reject */
 /* broker locks: enable(1), claims(2-4), output reads(5-9), phone read(10);
  * rollback releases gpc1(11)/gpx1(12)/gpy4(13) */
 m.fail_lock=fail_lock;
 int ie=i9100_modem_cp_init(&cp,1,&gpio,&io,1);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(ie==I9100_CP_IO);
 assert(s.fault&&!s.configured&&!s.enabled&&s.pins_owned==1U&&held->active);
 /* original cold rejection kept in error; rollback failure in cleanup_error */
 assert(s.error==I9100_CP_STATE&&s.cleanup_error==I9100_CP_IO);
 /* close while the rollback is still BUSY retains + reports honestly */
 m.fail_lock=m.locks+1U; /* held-bank release inside close */
 assert(i9100_modem_cp_close(&cp)==I9100_CP_IO);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.pins_owned==1U&&held->active);
 /* retry after the busy window: RESTORE continues per remaining bank */
 m.fail_lock=0;
 assert(i9100_modem_cp_close(&cp)==0);
 assert(!cp.gpc1.active&&!cp.gpx1.active&&!cp.gpy4.active);
 assert(i9100_modem_cp_close(&cp)==0); /* zero-resource close: no IO */
 assert(!i9100_gpio_broker_enable(&gpio,0));
}
static void cold_rollback_residual(void)
{
 cold_rollback_case(11U,&cp.gpc1);
 cold_rollback_case(12U,&cp.gpx1);
 cold_rollback_case(13U,&cp.gpy4);
}
static void claim_rollback_case(unsigned fail_lock,struct i9100_gpio_lease *held)
{
 /* Claim refusal mid-init + rollback release failure: same per-bank
  * RESTORE continuation applies to resources never configured. */
 setup();
 struct i9100_gpio_lease foreign={};
 assert(!i9100_gpio_claim(&gpio,I9100_GPY4,1U<<2,&foreign));
 /* locks: enable(1), foreign claim(2), init claims gpc1(3)/gpx1(4)/
  * gpy4-refused(5); rollback releases gpc1(6)/gpx1(7) */
 m.fail_lock=fail_lock;
 assert(i9100_modem_cp_init(&cp,1,&gpio,&io,1)==I9100_CP_IO);
 assert(held->active);
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s)&&
        s.error==I9100_CP_BUSY&&s.cleanup_error==I9100_CP_IO&&s.pins_owned==1U);
 m.fail_lock=0;
 assert(i9100_modem_cp_close(&cp)==0);
 assert(!held->active);
 assert(!i9100_gpio_release(&foreign,I9100_GPIO_RESTORE));
 assert(!i9100_gpio_broker_enable(&gpio,0));
}
static void claim_rollback_residual(void)
{
 claim_rollback_case(6U,&cp.gpc1);
 claim_rollback_case(7U,&cp.gpx1);
}
static void auto_safeoff_pulse(void)
{
 /* Probe F2: the 60us pulse delay failure must itself lower CP_ON;
  * no extra cancel may be required to converge the outputs. */
 setup();open_cp();m.fail_delay=4U;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 advance(500000U);
 assert(i9100_modem_cp_tick(&cp)==I9100_CP_IO);
 assert(i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)==0U); /* auto safe-off ran */
 struct i9100_modem_cp_status s;assert(!i9100_modem_cp_status(&cp,&s));
 assert(s.fault&&s.error==I9100_CP_IO&&s.state==I9100_CP_FAILED&&!s.cleanup_error);
 assert(i9100_modem_cp_cancel(&cp)==0); /* idempotent on converged outputs */
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_STATE); /* no faulted restart */
 close_cp();
 /* auto-off failure keeps first error + cleanup error + all resources */
 setup();open_cp();m.fail_delay=4U;
 assert(i9100_modem_cp_start(&cp,I9100_CP_OP_ON)==I9100_CP_BUSY);
 gpio.banks[I9100_GPX1].owners[0]=0; /* stolen owner blocks GPX1 safe-off */
 advance(500000U);
 assert(i9100_modem_cp_tick(&cp)==I9100_CP_IO);
 assert(i9100_cp_level(&m,CP_SLOT_GPC1,GPC1_CP_ON)==0U); /* owned bank still cleared */
 /* refused bank never written: the op-driven CP_RESET stays at its level */
 assert(i9100_cp_level(&m,CP_SLOT_GPX1,GPX1_RST)==1U);
 assert(!i9100_modem_cp_status(&cp,&s)&&s.error==I9100_CP_IO&&
        s.cleanup_error==I9100_CP_OWNER&&s.pins_owned==3U);
 gpio.banks[I9100_GPX1].owners[0]=cp.gpx1.generation;
 assert(i9100_modem_cp_cancel(&cp)==0);
 close_cp();
}
int main()
{
 lifecycle();power_on();reset_seq();wake_seq();wake_timeout();cancel_windows();
 fault_injection();cold_reject();partial_claim();neighbors();owner_isolation();cleanup_retry();
 high_pulse_recovery();partial_release();partial_init_retain();poll_integrity();clock_boundaries();
 cold_rollback_residual();claim_rollback_residual();auto_safeoff_pulse();
 puts("PASS EUR XMM6260 CP control: vendor on/reset/off/wake order+timing, bounded waits, "
      "cancel/idempotent, fault retention, cold-state reject, partial claim, neighbors, "
      "owner isolation, cleanup retry; models only, no CP/hardware evidence");
 return 0;
}
