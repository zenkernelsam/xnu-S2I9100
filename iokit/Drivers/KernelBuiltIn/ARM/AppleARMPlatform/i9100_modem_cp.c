/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_modem_cp.h"
#include "i9100_peripheral_board.h"
#include <string.h>
#define CP_INLINE_WAIT_US 250U
#define CP_STALL_MAX 1024U /* consecutive frozen-clock ticks before TIMEOUT */
#define CP_DELAY_SLACK_NS 50000000ULL
/* Engineering bound, not a vendor constant: no vendor step needs more than
 * ~520ms total, so 3s is a generous cap for a caller that lost ticks. */
#define CP_OP_BUDGET_NS 3000000000ULL
#define CP_WAKE_POLLS 20U
#define CP_WAKE_STEP_US 5000U
#define CP_BANK_GPC1 0U
#define CP_BANK_GPX1 1U
#define CP_BANK_GPY4 2U
enum {P_NONE,P_CP_ON,P_CP_RESET,P_REQ_RST,P_PDA,P_SWK,P_PHONE,P_HWAKE,P_DUMP,P_SUSPEND};
struct cp_step {uint8_t pa,pb;int8_t va,vb;uint32_t wait_us;};
static const struct cp_step on_steps[]={
 {P_CP_ON,P_CP_RESET,0,0,160U},{P_PDA,P_NONE,0,0,500000U},{P_CP_RESET,P_NONE,1,0,160U},
 {P_REQ_RST,P_NONE,1,0,160U},{P_CP_ON,P_NONE,1,0,60U},{P_CP_ON,P_NONE,0,0,20000U},{P_PDA,P_NONE,1,0,0U}};
static const struct cp_step reset_steps[]={
 {P_CP_RESET,P_REQ_RST,0,0,20000U},{P_CP_RESET,P_NONE,1,0,160U},{P_REQ_RST,P_NONE,1,0,100U},
 {P_CP_ON,P_NONE,1,0,60U},{P_CP_ON,P_NONE,0,0,20000U}};
static const struct cp_step off_steps[]={{P_CP_ON,P_CP_RESET,0,0,0U}};
static const unsigned banks[3]={I9100_GPC1,I9100_GPX1,I9100_GPY4};
static const unsigned masks[3]={1U<<1,(1U<<0)|(1U<<1)|(1U<<2)|(1U<<3)|(1U<<4)|(1U<<6),(1U<<2)|(1U<<6)};
static struct i9100_gpio_lease *lease_of(struct i9100_modem_cp *c,unsigned code,unsigned *pin)
{
 static const unsigned bank_of[]={0,CP_BANK_GPC1,CP_BANK_GPX1,CP_BANK_GPY4,CP_BANK_GPY4,CP_BANK_GPX1,
  CP_BANK_GPX1,CP_BANK_GPX1,CP_BANK_GPX1,CP_BANK_GPX1};
 static const unsigned pin_of[]={0,1,4,6,2,0,6,1,2,3};
 if(!code||code>P_SUSPEND)return 0;
 *pin=pin_of[code];
 return bank_of[code]==CP_BANK_GPC1?&c->gpc1:bank_of[code]==CP_BANK_GPX1?&c->gpx1:&c->gpy4;
}
static int pin_write(struct i9100_modem_cp *c,unsigned code,unsigned level)
{
 unsigned pin;struct i9100_gpio_lease *l=lease_of(c,code,&pin);
 return !l||i9100_gpio_output(l,pin,level)?I9100_CP_IO:0;
}
static int pin_read(struct i9100_modem_cp *c,unsigned code,unsigned *value)
{
 unsigned pin;struct i9100_gpio_lease *l=lease_of(c,code,&pin);
 return !l||i9100_gpio_level(l,pin,value)?I9100_CP_IO:0;
}
static int overlaps(uintptr_t a,unsigned an,uintptr_t b,unsigned bn)
{return a>UINTPTR_MAX-an||b>UINTPTR_MAX-bn||(a+an>b&&b+bn>a);}
static int live(struct i9100_modem_cp *c)
{return c&&c->initialized&&c->self==(uintptr_t)c&&c->gpio;}
static int take(struct i9100_modem_cp *c)
{return !live(c)?I9100_CP_ARGUMENT:c->io.lock(c->io.cookie)?I9100_CP_BUSY:0;}
static void drop(struct i9100_modem_cp *c){c->io.unlock(c->io.cookie);}
static int fail(struct i9100_modem_cp *c,int e)
{c->fault=1;if(!c->error)c->error=e;c->op=I9100_CP_OP_NONE;c->pending=0;
 if(c->state!=I9100_CP_CLOSING)c->state=I9100_CP_FAILED;return e;}
static int checked_delay(struct i9100_modem_cp *c,uint32_t us)
{
 uint64_t started=c->io.now_ns(c->io.cookie),minimum=(uint64_t)us*1000U;
 if(c->io.delay_us(c->io.cookie,us))return I9100_CP_IO;
 uint64_t now=c->io.now_ns(c->io.cookie);
 if(now<started||now-started<minimum||now-started>minimum+CP_DELAY_SLACK_NS)return I9100_CP_TIMEOUT;
 c->last_ns=now;return 0;
}
static int arm_wait(struct i9100_modem_cp *c,uint32_t us)
{
 uint64_t now=c->io.now_ns(c->io.cookie),d=(uint64_t)us*1000U;
 if(now<c->last_ns||now>UINT64_MAX-d)return I9100_CP_TIMEOUT;
 c->due_ns=now+d;c->pending=1;c->stalled=0;c->prev_tick_ns=now;return 0;
}
static struct i9100_gpio_lease *bank_lease(struct i9100_modem_cp *c,unsigned b)
{return b==0U?&c->gpc1:b==1U?&c->gpx1:&c->gpy4;}
/* Drive one bank's still-owned outputs low. A bank already returned to the
 * broker is skipped entirely so retry after a partial release never writes a
 * pin this object no longer owns; a foreign or copied owner is refused. */
static int bank_off(struct i9100_modem_cp *c,unsigned b)
{
 static const unsigned char out_pins[3][3]={{1,0xff,0xff},{0,4,0xff},{2,6,0xff}};
 struct i9100_gpio_lease *l=bank_lease(c,b);
 if(!l->active)return 0;
 struct i9100_gpio_broker *g=l->broker;
 if(!g||l->generation>g->generation)return I9100_CP_OWNER;
 for(unsigned p=0;p<8U;p++)if(l->mask&(1U<<p)&&g->banks[l->bank].owners[p]!=l->generation)
  return I9100_CP_OWNER;
 int e=0;
 for(unsigned n=0;n<3U&&out_pins[b][n]!=0xff;n++)
  if(i9100_gpio_output(l,out_pins[b][n],0))e=I9100_CP_IO;
 return e;
}
static int stop_all(struct i9100_modem_cp *c)
{
 int e=0,r;
 for(unsigned b=0;b<3U;b++){r=bank_off(c,b);if(r&&!e)e=r;}
 return e;
}
/* The faulted-op path itself attempts the accurate owned safe-off: a failed
 * inline pulse (or any step/wait error) must not leave CP_ON driven pending
 * a later cancel. First error stays in error; a failed stop is recorded in
 * cleanup_error while leases/parent remain for same-owner retry. */
static int fail_off(struct i9100_modem_cp *c,int e)
{
 e=fail(c,e);
 if(c->configured){int r=stop_all(c);if(r)c->cleanup_error=r;}
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
static int cold_state(struct i9100_modem_cp *c)
{
 /* A possibly running CP must never be silently adopted: every owned output
  * must read low and every observed input must be at its idle level. Saved
  * mux above 1 means an inherited peripheral function we cannot restore. */
 static const unsigned outputs[]={P_CP_ON,P_CP_RESET,P_REQ_RST,P_PDA,P_SWK};
 for(unsigned n=0;n<5U;n++){
  unsigned pin;struct i9100_gpio_lease *l=lease_of(c,outputs[n],&pin);
  if((l->saved[0]>>(4U*pin)&15U)>1U)return I9100_CP_STATE;
  unsigned value;if(pin_read(c,outputs[n],&value)||value)return I9100_CP_STATE;
 }
 static const unsigned inputs[]={P_PHONE,P_HWAKE,P_DUMP,P_SUSPEND};
 static const unsigned idle_level[]={0,1,0,0};
 for(unsigned n=0;n<4U;n++){
  unsigned pin;struct i9100_gpio_lease *l=lease_of(c,inputs[n],&pin);
  if((l->saved[0]>>(4U*pin)&15U)>1U)return I9100_CP_STATE;
  unsigned value;if(pin_read(c,inputs[n],&value)||value!=idle_level[n])return I9100_CP_STATE;
 }
 return 0;
}
static int release_all(struct i9100_modem_cp *c,enum i9100_gpio_release_policy policy)
{
 int e=0;struct i9100_gpio_lease *ls[3]={&c->gpc1,&c->gpx1,&c->gpy4};
 for(unsigned n=0;n<3U;n++)if(ls[n]->active&&i9100_gpio_release(ls[n],policy))e=I9100_CP_IO;
 return e;
}
static int close_locked(struct i9100_modem_cp *c)
{
 c->closing=1;c->ready=0;c->op=I9100_CP_OP_NONE;c->pending=0;c->state=I9100_CP_CLOSING;
 int e;
 if(c->configured){
  /* Written pins: accurately owned safe-off, then keep the low state. */
  e=stop_all(c);if(e){c->cleanup_error=e;return e;}
  e=release_all(c,I9100_GPIO_KEEP);if(e){c->cleanup_error=e;return e;}
 }else{
  /* Partial-init residue never rewrote a pin: continue the original
   * RESTORE rollback for every bank still held; a close may only return
   * success once all three leases are provably inactive. */
  e=release_all(c,I9100_GPIO_RESTORE);if(e){c->cleanup_error=e;return e;}
 }
 c->configured=0;c->enabled=0;return 0;
}
static int step_table(struct i9100_modem_cp *c,const struct cp_step *steps,unsigned count)
{
 while(c->step<count){
  const struct cp_step *s=&steps[c->step];
  int e=0;
  if(s->pa&&pin_write(c,s->pa,(unsigned)s->va))e=I9100_CP_IO;
  if(!e&&s->pb&&pin_write(c,s->pb,(unsigned)s->vb))e=I9100_CP_IO;
  if(e)return e;
  c->step++;c->last_ns=c->io.now_ns(c->io.cookie);
  if(!s->wait_us)continue;
  if(s->wait_us<=CP_INLINE_WAIT_US){e=checked_delay(c,s->wait_us);if(e)return e;continue;}
  if(arm_wait(c,s->wait_us))return I9100_CP_TIMEOUT;
  return I9100_CP_BUSY;
 }
 return 0;
}
static int step_wake(struct i9100_modem_cp *c)
{
 unsigned level;
 for(;;){
  switch(c->step){
  case 0: /* Already active-low hostwake: nothing to pulse. */
   if(pin_read(c,P_HWAKE,&level))return I9100_CP_IO;
   if(!level)return 0;
   if(pin_read(c,P_SWK,&level))return I9100_CP_IO;
   c->step=level?1U:2U;continue;
  case 1:
   if(pin_write(c,P_SWK,0))return I9100_CP_IO;
   c->step=2;
   if(arm_wait(c,CP_WAKE_STEP_US))return I9100_CP_TIMEOUT;
   return I9100_CP_BUSY;
  case 2:
   if(pin_write(c,P_SWK,1))return I9100_CP_IO;
   c->step=3;c->poll=0;
   if(arm_wait(c,CP_WAKE_STEP_US))return I9100_CP_TIMEOUT;
   return I9100_CP_BUSY;
  default:
   if(pin_read(c,P_HWAKE,&level))return I9100_CP_IO;
   if(!level)return 0;
   if(++c->poll>=CP_WAKE_POLLS)return I9100_CP_TIMEOUT;
   if(arm_wait(c,CP_WAKE_STEP_US))return I9100_CP_TIMEOUT;
   return I9100_CP_BUSY;
  }
 }
}
static int run_op(struct i9100_modem_cp *c)
{
 int e;
 switch(c->op){
 case I9100_CP_OP_ON:e=step_table(c,on_steps,7U);if(!e)c->state=I9100_CP_BOOTING;break;
 case I9100_CP_OP_RESET:e=step_table(c,reset_steps,5U);if(!e)c->state=I9100_CP_BOOTING;break;
 case I9100_CP_OP_OFF:e=step_table(c,off_steps,1U);if(!e)c->state=I9100_CP_OFF;break;
 case I9100_CP_OP_WAKE:e=step_wake(c);break;
 default:return I9100_CP_STATE;
 }
 if(e==I9100_CP_BUSY)return e;
 if(e)return fail_off(c,e);
 c->op=I9100_CP_OP_NONE;c->op_started_ns=0;c->seq++;
 return 0;
}
int i9100_modem_cp_init(struct i9100_modem_cp *c,int enabled,struct i9100_gpio_broker *gpio,
 const struct i9100_modem_cp_io *io,int verified)
{
 if(!c||c->initialized||(enabled!=0&&enabled!=1))return I9100_CP_ARGUMENT;
 if(!enabled)return I9100_CP_DISABLED;
 if(verified!=1||!profile_valid(gpio)||!io||!io->lock||!io->unlock||!io->now_ns||!io->delay_us||
    overlaps((uintptr_t)c,sizeof(*c),(uintptr_t)gpio,sizeof(*gpio))||
    overlaps((uintptr_t)c,sizeof(*c),(uintptr_t)io,sizeof(*io)))return I9100_CP_ARGUMENT;
 if(io->lock(io->cookie))return I9100_CP_BUSY;
 memset(c,0,sizeof(*c));c->io=*io;c->gpio=gpio;c->self=(uintptr_t)c;c->initialized=1;c->stage=1;
 int e=0;
 for(unsigned n=0;n<3U&&!e;n++)
  if(i9100_gpio_claim(gpio,banks[n],masks[n],n==0U?&c->gpc1:n==1U?&c->gpx1:&c->gpy4))e=I9100_CP_BUSY;
 if(!e)e=cold_state(c);
 if(e){
  /* Original rejection stays in error; a failed RESTORE rollback is the
   * actionable result and is also recorded separately in cleanup_error. */
  int r=release_all(c,I9100_GPIO_RESTORE);
  c->error=e;c->cleanup_error=r;c->fault=1;c->state=I9100_CP_FAILED;
  io->unlock(io->cookie);return r?r:e;
 }
 c->configured=1;c->stage=2;
 e=pin_write(c,P_REQ_RST,0);if(!e)e=pin_write(c,P_CP_ON,0);if(!e)e=pin_write(c,P_CP_RESET,0);
 if(!e&&i9100_gpio_pull(&c->gpx1,4,0))e=I9100_CP_IO;
 if(!e)e=pin_write(c,P_PDA,0);if(!e)e=pin_write(c,P_SWK,0);
 if(!e&&i9100_gpio_input(&c->gpx1,6))e=I9100_CP_IO;
 if(!e&&i9100_gpio_pull(&c->gpx1,6,0))e=I9100_CP_IO;
 if(!e&&i9100_gpio_input(&c->gpx1,1))e=I9100_CP_IO;
 if(!e&&i9100_gpio_pull(&c->gpx1,1,0))e=I9100_CP_IO;
 if(!e&&i9100_gpio_input(&c->gpx1,2))e=I9100_CP_IO;
 if(!e&&i9100_gpio_input(&c->gpx1,3))e=I9100_CP_IO;
 if(e){
  int r=stop_all(c);if(!r)r=release_all(c,I9100_GPIO_KEEP);
  c->error=e;c->cleanup_error=r;c->fault=1;c->state=I9100_CP_FAILED;
  io->unlock(io->cookie);return e;
 }
 c->enabled=c->ready=1;c->state=I9100_CP_OFF;c->stage=3;
 io->unlock(io->cookie);return 0;
}
int i9100_modem_cp_start(struct i9100_modem_cp *c,enum i9100_cp_op op)
{
 if(op<I9100_CP_OP_ON||op>I9100_CP_OP_WAKE)return I9100_CP_ARGUMENT;
 int e=take(c);if(e)return e;
 if(!c->enabled){drop(c);return I9100_CP_DISABLED;}
 if(c->fault||c->closing||c->state==I9100_CP_FAILED){drop(c);return I9100_CP_STATE;}
 if(c->op){drop(c);return I9100_CP_BUSY;}
 int allowed=(op==I9100_CP_OP_ON&&c->state==I9100_CP_OFF)||
  ((op==I9100_CP_OP_RESET||op==I9100_CP_OP_OFF||op==I9100_CP_OP_WAKE)&&c->state==I9100_CP_BOOTING);
 if(!allowed){drop(c);return I9100_CP_STATE;}
 c->op=op;c->step=0;c->poll=0;c->pending=0;c->stalled=0;
 c->op_started_ns=c->last_ns=c->prev_tick_ns=c->io.now_ns(c->io.cookie);
 drop(c);return i9100_modem_cp_tick(c);
}
int i9100_modem_cp_tick(struct i9100_modem_cp *c)
{
 int e=take(c);if(e)return e;
 if(!c->enabled){drop(c);return I9100_CP_DISABLED;}
 if(!c->op){drop(c);return 0;}
 if(c->closing||c->state==I9100_CP_FAILED){c->op=I9100_CP_OP_NONE;c->pending=0;drop(c);return I9100_CP_STATE;}
 uint64_t now=c->io.now_ns(c->io.cookie);
 if(now<c->last_ns){e=fail_off(c,I9100_CP_TIMEOUT);drop(c);return e;}
 c->last_ns=now;
 if(c->pending){
  if(now<c->due_ns){
   /* Freeze strikes only: a normal wait must not time out merely because
    * the caller ticked often while its clock advanced. */
   if(now==c->prev_tick_ns){if(++c->stalled>CP_STALL_MAX){e=fail_off(c,I9100_CP_TIMEOUT);drop(c);return e;}}
   else c->stalled=0;
   c->prev_tick_ns=now;drop(c);return I9100_CP_BUSY;
  }
  /* A hugely-late resume must not start fresh writes after the vendor
   * timing window; the op is bounded by a documented total budget. */
  if(now-c->op_started_ns>CP_OP_BUDGET_NS){e=fail_off(c,I9100_CP_TIMEOUT);drop(c);return e;}
  c->pending=0;
 }
 e=run_op(c);drop(c);return e;
}
int i9100_modem_cp_cancel(struct i9100_modem_cp *c)
{
 int e=take(c);if(e)return e;
 if(!c->enabled){drop(c);return I9100_CP_DISABLED;}
 if(c->op){c->cancels++;c->op=I9100_CP_OP_NONE;c->pending=0;}
 /* An op-free caller is not proof the outputs are low: a faulted or
  * half-driven sequence still gets an accurately owned safe-off attempt. */
 e=c->configured?stop_all(c):0;
 if(e){fail(c,e);drop(c);return e;}
 if(!c->fault)c->state=I9100_CP_OFF;
 drop(c);return 0;
}
int i9100_modem_cp_poll(struct i9100_modem_cp *c,struct i9100_modem_cp_pins *out)
{
 if(!out||!c||overlaps((uintptr_t)c,sizeof(*c),(uintptr_t)out,sizeof(*out)))return I9100_CP_ARGUMENT;
 int e=take(c);if(e)return e;
 if(!c->enabled){drop(c);return I9100_CP_DISABLED;}
 /* Local snapshot first: a mid-read failure must not publish partial
  * levels, stale caller data, or consume a successful seq. */
 struct i9100_modem_cp_pins tmp={0};unsigned value=0;int r=0;
 r|=pin_read(c,P_CP_ON,&value);tmp.cp_on=(uint8_t)value;
 r|=pin_read(c,P_CP_RESET,&value);tmp.cp_reset=(uint8_t)value;
 r|=pin_read(c,P_REQ_RST,&value);tmp.req_reset=(uint8_t)value;
 r|=pin_read(c,P_PDA,&value);tmp.pda_active=(uint8_t)value;
 r|=pin_read(c,P_SWK,&value);tmp.slave_wake=(uint8_t)value;
 r|=pin_read(c,P_PHONE,&value);tmp.phone_active=(uint8_t)value;
 r|=pin_read(c,P_HWAKE,&value);tmp.host_wake=(uint8_t)value;
 r|=pin_read(c,P_DUMP,&value);tmp.dump_int=(uint8_t)value;
 r|=pin_read(c,P_SUSPEND,&value);tmp.suspend_req=(uint8_t)value;
 if(r){fail(c,I9100_CP_IO);drop(c);return I9100_CP_IO;}
 tmp.seq=++c->seq;tmp.time_ns=c->io.now_ns(c->io.cookie);
 *out=tmp;drop(c);return 0;
}
int i9100_modem_cp_status(struct i9100_modem_cp *c,struct i9100_modem_cp_status *out)
{
 if(!out||!c||overlaps((uintptr_t)c,sizeof(*c),(uintptr_t)out,sizeof(*out)))return I9100_CP_ARGUMENT;
 int e=take(c);if(e)return e;
 memset(out,0,sizeof(*out));
 out->initialized=c->initialized;out->enabled=c->enabled;out->ready=c->ready;out->state=c->state;
 out->closing=c->closing;out->fault=c->fault;out->configured=c->configured;out->op=c->op;out->step=c->step;
 out->poll=c->poll;out->stage=c->stage;out->seq=c->seq;out->cancels=c->cancels;
 out->op_started_ns=c->op_started_ns;out->due_ns=c->due_ns;out->last_ns=c->last_ns;
 out->error=c->error;out->cleanup_error=c->cleanup_error;
 out->pins_owned=(c->gpc1.active?1U:0U)+(c->gpx1.active?1U:0U)+(c->gpy4.active?1U:0U);
 drop(c);return 0;
}
int i9100_modem_cp_close(struct i9100_modem_cp *c)
{
 int e=take(c);if(e)return e;
 /* Zero-resource/disabled close exits without IO. Anything still claimed,
  * configured or enabled keeps full cleanup qualification regardless of
  * the enabled flag (partial-init failures retain active leases). */
 if(!c->enabled&&!c->configured&&!c->gpc1.active&&!c->gpx1.active&&!c->gpy4.active){
  c->closing=1;c->ready=0;drop(c);return 0;
 }
 e=close_locked(c);c->cleanup_error=e;if(e)c->fault=1;drop(c);return e;
}
