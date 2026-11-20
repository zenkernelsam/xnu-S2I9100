/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_power_shared.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSObject.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef absolutetime_to_nanoseconds
extern "C" {
#include <kern/thread.h>
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
enum {PREPARING=0x10000000U,READY=0x20000000U,FAILED=0x30000000U,MODE_MASK=0xf0000000U,LEGACY_MAX=65535U};
struct read_context {OSObject *owner;i9100_power_hw *hw;uint32_t generation,refs,open;i9100_power_haptic_ops haptic;};
static volatile uint32_t state;
static IOLock *bus_lock[2],*registry_lock;
static i9100_power_bus_ticket *bus_owner[2];
static read_context *published;
static OSObject *volatile published_owner,*volatile withdrawn_owner;
static uint32_t generation;
static i9100_power_haptic_client *haptic_owner;
static struct {uintptr_t thread;unsigned slave,reg,live_required;uint8_t value;volatile uint32_t active;} haptic_authorization;
static bool source_live(read_context *);
static bool thread_ok(){return ml_get_interrupts_enabled()&&!ml_at_interrupt_context();}
static uint64_t now_ns(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
static uint32_t mode(){return __sync_fetch_and_add(&state,0);}
static int lock_bounded(IOLock *lock)
{
 uint64_t begin=now_ns();for(unsigned n=0;n<1000U;n++){
  if(IOLockTryLock(lock))return 0;uint64_t now=now_ns();if(now<begin||now-begin>=100000000ULL)break;IODelay(100);
 }return I9100_SHARED_BUSY;
}
int i9100_power_shared_prepare(int enabled)
{
 if(enabled!=0&&enabled!=1)return I9100_SHARED_ARGUMENT;if(!enabled)return I9100_SHARED_DISABLED;
 if(!thread_ok())return I9100_SHARED_ARGUMENT;uint32_t old=mode();if(old==READY)return 0;
 if(!__sync_bool_compare_and_swap(&state,0,PREPARING))return old==FAILED?I9100_SHARED_IO:I9100_SHARED_BUSY;
 bus_lock[0]=IOLockAlloc();bus_lock[1]=IOLockAlloc();registry_lock=IOLockAlloc();
 if(!bus_lock[0]||!bus_lock[1]||!registry_lock){
  for(unsigned n=0;n<2;n++)if(bus_lock[n]){IOLockFree(bus_lock[n]);bus_lock[n]=0;}
  if(registry_lock){IOLockFree(registry_lock);registry_lock=0;}__sync_lock_test_and_set(&state,FAILED);return I9100_SHARED_IO;
 }
 __sync_lock_test_and_set(&state,READY);return 0;
}
int i9100_power_shared_enabled(){return mode()==READY;}
int i9100_power_bus_enter(unsigned bus,i9100_power_bus_ticket *ticket)
{
 if(bus>1U||!ticket||ticket->owned||(ticket->self&&ticket->self!=(uintptr_t)ticket)||!thread_ok())return 0;
 for(unsigned attempt=0;attempt<1000U;attempt++){
  uint32_t old=mode();
  if(!(old&MODE_MASK)){
   if(old>=LEGACY_MAX)return 0;if(!__sync_bool_compare_and_swap(&state,old,old+1U))continue;
   memset(ticket,0,sizeof(*ticket));ticket->self=(uintptr_t)ticket;ticket->thread=(uintptr_t)current_thread();ticket->bus=bus;ticket->mode=1;ticket->owned=1;return 1;
  }
  if(old!=READY||lock_bounded(bus_lock[bus]))return 0;
  memset(ticket,0,sizeof(*ticket));ticket->self=(uintptr_t)ticket;ticket->thread=(uintptr_t)current_thread();ticket->bus=bus;ticket->mode=2;ticket->owned=1;bus_owner[bus]=ticket;return 1;
 }return 0;
}
int i9100_power_bus_leave(i9100_power_bus_ticket *ticket)
{
 if(!ticket||!ticket->owned||ticket->self!=(uintptr_t)ticket||ticket->bus>1U||ticket->thread!=(uintptr_t)current_thread())return 0;
 if(ticket->mode==1U){
  for(unsigned n=0;n<1000U;n++){uint32_t old=mode();if(!old||(old&MODE_MASK))return 0;if(__sync_bool_compare_and_swap(&state,old,old-1U)){ticket->owned=0;return 1;}}
  return 0;
 }
 if(ticket->mode!=2U||bus_owner[ticket->bus]!=ticket)return 0;bus_owner[ticket->bus]=0;ticket->owned=0;IOLockUnlock(bus_lock[ticket->bus]);return 1;
}
static int publish(void *object,i9100_power_hw *hw,const i9100_power_haptic_ops *ops)
{
 if(!object||!hw||!hw->cookie||!hw->read32||!hw->write32||!hw->delay_us||!hw->enter||!hw->leave||!thread_ok())return I9100_SHARED_ARGUMENT;
 if(mode()!=READY)return I9100_SHARED_DISABLED;int e=lock_bounded(registry_lock);if(e)return e;
 if(published||generation==0xffffffffU){IOLockUnlock(registry_lock);return I9100_SHARED_BUSY;}
 read_context *ctx=(read_context *)IOMalloc(sizeof(*ctx));if(!ctx){IOLockUnlock(registry_lock);return I9100_SHARED_IO;}
 memset(ctx,0,sizeof(*ctx));if(ops)ctx->haptic=*ops;
 ctx->owner=(OSObject *)object;ctx->hw=hw;ctx->generation=++generation;ctx->refs=ctx->open=1;ctx->owner->retain();published=ctx;
 __sync_lock_test_and_set(&withdrawn_owner,(OSObject *)0);__sync_lock_test_and_set(&published_owner,ctx->owner);
 IOLockUnlock(registry_lock);return 0;
}
int i9100_power_shared_publish(void *object,i9100_power_hw *hw){return publish(object,hw,0);}
int i9100_power_shared_publish_haptic(void *object,i9100_power_hw *hw,const i9100_power_haptic_ops *ops)
{return ops&&ops->read_config&&ops->write_config&&ops->guarded_wire==1U?publish(object,hw,ops):I9100_SHARED_ARGUMENT;}
int i9100_power_shared_unpublish(void *object)
{
 if(!object||!thread_ok())return I9100_SHARED_ARGUMENT;if(mode()!=READY)return I9100_SHARED_DISABLED;
 OSObject *current=__sync_val_compare_and_swap(&published_owner,(OSObject *)0,(OSObject *)0);
 if(current!=object)return current?I9100_SHARED_ARGUMENT:0;
 /* Revoke before registry cleanup. Contention keeps the publication's
  * owner reference alive until an explicit unpublish completes. */
 __sync_lock_test_and_set(&withdrawn_owner,(OSObject *)object);
 int e=lock_bounded(registry_lock);if(e)return e;
 read_context *ctx=published;if(!ctx){IOLockUnlock(registry_lock);return 0;}
 if(ctx->owner!=object){IOLockUnlock(registry_lock);return I9100_SHARED_ARGUMENT;}
 published=0;__sync_lock_test_and_set(&published_owner,(OSObject *)0);__sync_lock_test_and_set(&ctx->open,0);ctx->refs--;OSObject *owner=ctx->owner;bool destroy=!ctx->refs;
 IOLockUnlock(registry_lock);owner->release();if(destroy)IOFree(ctx,sizeof(*ctx));return 0;
}
int i9100_power_read_open(i9100_power_read_client *client)
{
 if(!client||client->owned||(client->self&&client->self!=(uintptr_t)client)||!thread_ok())return I9100_SHARED_ARGUMENT;
 if(mode()!=READY)return I9100_SHARED_DISABLED;int e=lock_bounded(registry_lock);if(e)return e;
 read_context *ctx=published;if(!ctx||!ctx->open||ctx->refs>32U||ctx->owner==__sync_val_compare_and_swap(&withdrawn_owner,(OSObject *)0,(OSObject *)0)){IOLockUnlock(registry_lock);return I9100_SHARED_NOT_READY;}
 ctx->refs++;ctx->owner->retain();memset(client,0,sizeof(*client));client->context=ctx;client->self=(uintptr_t)client;
 client->generation=ctx->generation;client->owned=1;IOLockUnlock(registry_lock);return 0;
}
static bool valid(i9100_power_read_client *c)
{return c&&c->owned&&c->self==(uintptr_t)c&&c->context&&c->generation==((read_context *)c->context)->generation;}
static bool safe_reg(unsigned reg){return reg<=1U||reg==0x13U||reg==0x14U||(reg>=0x3bU&&reg<=0x4dU);}
int i9100_power_read_pmic(i9100_power_read_client *client,unsigned reg,uint8_t *out)
{
 if(!out||!safe_reg(reg)||!thread_ok()||mode()!=READY)return I9100_SHARED_ARGUMENT;
 uintptr_t a=(uintptr_t)client,b=(uintptr_t)out;
 if(!client||a>~(uintptr_t)0-sizeof(*client)||(b>=a&&b<a+sizeof(*client)))return I9100_SHARED_ARGUMENT;
 int e=lock_bounded(registry_lock);if(e)return e;
 if(!valid(client)){IOLockUnlock(registry_lock);return I9100_SHARED_ARGUMENT;}
 read_context *ctx=(read_context *)client->context;
 if(!ctx->open||ctx!=published||ctx->owner==__sync_val_compare_and_swap(&withdrawn_owner,(OSObject *)0,(OSObject *)0)){IOLockUnlock(registry_lock);return I9100_SHARED_NOT_READY;}
 if(__sync_fetch_and_add(&client->busy,0)){IOLockUnlock(registry_lock);return I9100_SHARED_BUSY;}__sync_lock_test_and_set(&client->busy,1);
 i9100_power_hw *hw=ctx->hw;IOLockUnlock(registry_lock);uint8_t value=0;int ok=i9100_power_pmic_read(hw,reg,&value);
 /* Client busy pins this owner/context. Linearize at the atomic open check,
  * publish the result before dropping busy, then never touch context again. */
 e=!__sync_fetch_and_add(&ctx->open,0)||ctx->owner==__sync_val_compare_and_swap(&withdrawn_owner,(OSObject *)0,(OSObject *)0)?I9100_SHARED_NOT_READY:!ok?I9100_SHARED_IO:0;
 if(!e)*out=value;__sync_lock_release(&client->busy);return e;
}
int i9100_power_read_close(i9100_power_read_client *client)
{
 if(!thread_ok()||mode()!=READY)return I9100_SHARED_ARGUMENT;int e=lock_bounded(registry_lock);if(e)return e;
 if(!valid(client)){IOLockUnlock(registry_lock);return I9100_SHARED_ARGUMENT;}
 if(__sync_fetch_and_add(&client->busy,0)){IOLockUnlock(registry_lock);return I9100_SHARED_BUSY;}
 read_context *ctx=(read_context *)client->context;client->owned=0;ctx->refs--;OSObject *owner=ctx->owner;bool destroy=!ctx->refs;
 IOLockUnlock(registry_lock);owner->release();if(destroy)IOFree(ctx,sizeof(*ctx));return 0;
}

int i9100_power_bus_filter(i9100_power_bus_ticket *ticket,unsigned slave,unsigned reg,const uint8_t *data,unsigned count,int writing)
{
 if(!ticket||!ticket->owned||ticket->self!=(uintptr_t)ticket||ticket->thread!=(uintptr_t)current_thread()||ticket->bus>1U)return 0;
 /* New charge-epoch RTC sampling is a fixed read-only raw wire contract,
  * NOT a general external register reader. Keep exact CONTROL/7-byte time
  * reads through BUS0 while denying RTC writes/alarms/UPDATE and malformed
  * bursts before the legacy or enabled paths can touch physical GPIO. */
 if(slave==0x06U&&(!data||writing||ticket->bus!=0U||
    !((reg==0x03U&&count==1U)||(reg==0x10U&&count==7U))))return 0;
 if(ticket->mode==1U)return !haptic_owner;
 if(ticket->mode!=2U||bus_owner[ticket->bus]!=ticket)return 0;
 if(!writing||ticket->bus!=0U||!haptic_owner||!((slave==0x48U&&reg==2U)||(slave==0x66U&&reg==0x48U)))return 1;
 /* BUS0 pins the owner pointer through filter and wire IO. Close/install
  * take that same lock. Only one exact, thread-bound write can consume auth. */
 if(!data||count!=1U||!__sync_fetch_and_add(&haptic_authorization.active,0)||lock_bounded(registry_lock))return 0;
 bool allowed=__sync_fetch_and_add(&haptic_owner->busy,0)&&haptic_authorization.thread==(uintptr_t)current_thread()&&
    haptic_authorization.slave==slave&&haptic_authorization.reg==reg&&haptic_authorization.value==data[0]&&
    (!haptic_authorization.live_required||source_live((read_context *)haptic_owner->context));
 if(allowed)allowed=__sync_bool_compare_and_swap(&haptic_authorization.active,1U,0U);
 IOLockUnlock(registry_lock);return allowed;
}
static bool haptic_valid(i9100_power_haptic_client *client)
{return client&&client->owned&&client->self==(uintptr_t)client&&client->context&&haptic_owner==client&&
 client->generation==((read_context *)client->context)->generation;}
static bool source_live(read_context *ctx)
{return __sync_fetch_and_add(&ctx->open,0)&&ctx->owner!=__sync_val_compare_and_swap(&withdrawn_owner,(OSObject *)0,(OSObject *)0);}
static int haptic_claim(i9100_power_haptic_client *client,int need_ready)
{
 if(!thread_ok()||mode()!=READY)return I9100_SHARED_ARGUMENT;
 int e=lock_bounded(registry_lock);if(e)return e;
 if(!haptic_valid(client))e=I9100_SHARED_ARGUMENT;
 else if(__sync_fetch_and_add(&client->busy,0))e=I9100_SHARED_BUSY;
 else if(need_ready&&!client->ready)e=I9100_SHARED_NOT_READY;
 else __sync_lock_test_and_set(&client->busy,1);
 IOLockUnlock(registry_lock);return e;
}
static int haptic_finish(i9100_power_haptic_client *client,int error,int live_required)
{
 if(!error&&live_required&&!source_live((read_context *)client->context))error=I9100_SHARED_NOT_READY;
 if(error&&error!=I9100_SHARED_BUSY&&error!=I9100_SHARED_ARGUMENT){client->fault=1;if(!client->error)client->error=error;}
 __sync_lock_release(&client->busy);return error;
}
static int authorized_write(i9100_power_haptic_client *client,unsigned slave,uint8_t value,int live_required)
{
 read_context *ctx=(read_context *)client->context;
 if(lock_bounded(registry_lock))return 0;
 haptic_authorization.thread=(uintptr_t)current_thread();haptic_authorization.slave=slave;
 haptic_authorization.reg=slave==0x48U?2U:0x48U;haptic_authorization.value=value;
 haptic_authorization.live_required=(unsigned)live_required;
 __sync_lock_test_and_set(&haptic_authorization.active,1);
 IOLockUnlock(registry_lock);
 int ok=slave==0x48U?ctx->haptic.write_config(ctx->hw,value):i9100_power_pmic_write(ctx->hw,0x48U,value);
 __sync_lock_release(&haptic_authorization.active);return ok;
}
static int haptic_release(i9100_power_haptic_client *client)
{
 i9100_power_bus_ticket ticket={};if(!i9100_power_bus_enter(0,&ticket))return haptic_finish(client,I9100_SHARED_BUSY,0);
 int e=lock_bounded(registry_lock);
 if(e){(void)i9100_power_bus_leave(&ticket);return haptic_finish(client,e,0);}
 if(!haptic_valid(client)||client->users){IOLockUnlock(registry_lock);(void)i9100_power_bus_leave(&ticket);return haptic_finish(client,I9100_SHARED_BUSY,0);}
 read_context *ctx=(read_context *)client->context;OSObject *owner=ctx->owner;
 haptic_owner=0;__sync_lock_release(&haptic_authorization.active);client->owned=client->ready=0;ctx->refs--;bool destroy=!ctx->refs;
 IOLockUnlock(registry_lock);int off=i9100_power_bus_leave(&ticket);__sync_lock_release(&client->busy);
 owner->release();if(destroy)IOFree(ctx,sizeof(*ctx));return off?0:I9100_SHARED_IO;
}
int i9100_power_haptic_open(i9100_power_haptic_client *client,int verified)
{
 if(!client||client->owned||(client->self&&client->self!=(uintptr_t)client)||verified!=1||!thread_ok())return I9100_SHARED_ARGUMENT;
 if(mode()!=READY)return I9100_SHARED_DISABLED;
 i9100_power_bus_ticket ticket={};if(!i9100_power_bus_enter(0,&ticket))return I9100_SHARED_BUSY;
 int e=lock_bounded(registry_lock);if(e){(void)i9100_power_bus_leave(&ticket);return e;}
 read_context *ctx=published;
 if(haptic_owner)e=I9100_SHARED_BUSY;
 else if(!ctx||!source_live(ctx)||ctx->refs>32U||!ctx->haptic.read_config||!ctx->haptic.write_config||ctx->haptic.guarded_wire!=1U)e=I9100_SHARED_NOT_READY;
 if(!e){ctx->refs++;ctx->owner->retain();memset(client,0,sizeof(*client));client->context=ctx;
  client->self=(uintptr_t)client;client->generation=ctx->generation;client->owned=client->busy=1;haptic_owner=client;}
 IOLockUnlock(registry_lock);int off=i9100_power_bus_leave(&ticket);if(e)return e;if(!off)return haptic_finish(client,I9100_SHARED_IO,0);
 uint8_t config=0,ldo=0;
 if(!ctx->haptic.read_config(ctx->hw,&config)||!i9100_power_pmic_read(ctx->hw,0x48U,&ldo))e=I9100_SHARED_IO;
 else if(config&0x40U)e=I9100_SHARED_BUSY;
 else if((ldo&0x3fU)!=40U||((ldo>>6)!=0U&&(ldo>>6)!=3U))e=I9100_SHARED_NOT_READY;
 else if(!source_live(ctx))e=I9100_SHARED_NOT_READY;
 if(!e){client->saved_config=config;client->saved_ldo=ldo;client->ready=1;__sync_lock_release(&client->busy);return 0;}
 client->error=e;int released=haptic_release(client);if(released)client->cleanup_error=released;return e;
}
int i9100_power_haptic_check(i9100_power_haptic_client *client)
{
 int e=haptic_claim(client,1);if(e)return e;
 if(client->fault)e=I9100_SHARED_IO;else if(!source_live((read_context *)client->context))e=I9100_SHARED_NOT_READY;
 __sync_lock_release(&client->busy);return e;
}
int i9100_power_haptic_read(i9100_power_haptic_client *client,uint8_t *out)
{
 if(!client||!out)return I9100_SHARED_ARGUMENT;uintptr_t a=(uintptr_t)client,b=(uintptr_t)out;
 if(a>~(uintptr_t)0-sizeof(*client)||(b>=a&&b<a+sizeof(*client)))return I9100_SHARED_ARGUMENT;
 int e=haptic_claim(client,1);if(e)return e;read_context *ctx=(read_context *)client->context;uint8_t value=0;
 if(!ctx->haptic.read_config(ctx->hw,&value))e=I9100_SHARED_IO;else *out=value;
 return haptic_finish(client,e,0); /* cleanup may read the retained revoked provider */
}
int i9100_power_haptic_write(i9100_power_haptic_client *client,uint8_t value)
{
 int e=haptic_claim(client,1);if(e)return e;read_context *ctx=(read_context *)client->context;
 bool enable=!!(value&0x40U),cleanup=value==(client->saved_config&~0x40U);
 if(value!=0xc3U&&value!=0x83U&&!cleanup)e=I9100_SHARED_ARGUMENT;
 else if(!cleanup&&(client->fault||!source_live(ctx)))e=I9100_SHARED_NOT_READY;
 else if(enable&&client->motor_owned)e=I9100_SHARED_BUSY;
 else if(enable&&!client->rail_owned)e=I9100_SHARED_ARGUMENT;
 uint8_t ldo=0,config=0;
 if(!e&&enable){
  if(!i9100_power_pmic_read(ctx->hw,0x48U,&ldo)||ldo!=(uint8_t)((client->saved_ldo&0x3fU)|0xc0U))e=I9100_SHARED_IO;
 }
 if(!e&&!cleanup&&!source_live(ctx))e=I9100_SHARED_NOT_READY;
 if(!e){
  if(enable)client->motor_owned=1;
  if(!authorized_write(client,0x48U,value,!cleanup)||!ctx->haptic.read_config(ctx->hw,&config)||config!=value)e=I9100_SHARED_IO;
  else if(!enable)client->motor_owned=0;
 }
 return haptic_finish(client,e,enable);
}
int i9100_power_haptic_rail(i9100_power_haptic_client *client,int enable)
{
 if(enable!=0&&enable!=1)return I9100_SHARED_ARGUMENT;
 int e=haptic_claim(client,1);if(e)return e;read_context *ctx=(read_context *)client->context;
 if(enable&&(client->fault||!source_live(ctx)))e=I9100_SHARED_NOT_READY;
 else if((enable&&client->rail_owned)||(!enable&&!client->rail_owned))e=I9100_SHARED_BUSY;
 uint8_t current=0,verify=0,target=enable?(uint8_t)((client->saved_ldo&0x3fU)|0xc0U):client->saved_ldo;
 if(!e){
  if(enable)client->rail_owned=1; /* error may still have powered the rail */
  if(!i9100_power_pmic_read(ctx->hw,0x48U,&current)||(current&0x3fU)!=(client->saved_ldo&0x3fU)||
     ((current>>6)!=0U&&(current>>6)!=3U))e=I9100_SHARED_IO;
  else if((current!=target&&!authorized_write(client,0x66U,target,enable))||!i9100_power_pmic_read(ctx->hw,0x48U,&verify)||verify!=target)e=I9100_SHARED_IO;
  else if(!enable)client->rail_owned=0;
 }
 return haptic_finish(client,e,enable);
}
int i9100_power_haptic_close(i9100_power_haptic_client *client)
{
 int e=haptic_claim(client,0);if(e)return e;
 if(client->users||client->rail_owned||client->motor_owned)return haptic_finish(client,I9100_SHARED_BUSY,0);
 if(client->ready){
  read_context *ctx=(read_context *)client->context;uint8_t config=0,ldo=0;
  if(!ctx->haptic.read_config(ctx->hw,&config)||!i9100_power_pmic_read(ctx->hw,0x48U,&ldo)||
     config!=(uint8_t)(client->saved_config&~0x40U)||ldo!=client->saved_ldo)return haptic_finish(client,I9100_SHARED_IO,0);
 }
 return haptic_release(client);
}
static int haptic_hold(i9100_power_haptic_client *client,i9100_power_haptic_hold *hold,unsigned operation)
{
 if(!client||!hold||!thread_ok()||mode()!=READY)return I9100_SHARED_ARGUMENT;
 uintptr_t a=(uintptr_t)client,b=(uintptr_t)hold;
 if(a>~(uintptr_t)0-sizeof(*client)||b>~(uintptr_t)0-sizeof(*hold)||
    (a+sizeof(*client)>b&&b+sizeof(*hold)>a))return I9100_SHARED_ARGUMENT;
 int e=lock_bounded(registry_lock);if(e)return e;
 if(!haptic_valid(client))e=I9100_SHARED_ARGUMENT;
 else if(__sync_fetch_and_add(&client->busy,0))e=I9100_SHARED_BUSY;
 else if(operation==0U){
  if(hold->owned||(hold->self&&hold->self!=(uintptr_t)hold))e=I9100_SHARED_ARGUMENT;
  else if(!client->ready||client->fault||!source_live((read_context *)client->context))e=I9100_SHARED_NOT_READY;
  else if(client->users==0xffffffffU)e=I9100_SHARED_EXHAUSTED;
  else{memset(hold,0,sizeof(*hold));hold->client=client;hold->self=(uintptr_t)hold;hold->generation=client->generation;hold->owned=1;client->users++;}
 }else if(!hold->owned||hold->self!=(uintptr_t)hold||hold->client!=client||hold->generation!=client->generation||!client->users)e=I9100_SHARED_ARGUMENT;
 else if(operation==1U){if(client->fault)e=I9100_SHARED_IO;else if(!source_live((read_context *)client->context))e=I9100_SHARED_NOT_READY;}
 else{client->users--;hold->owned=0;}
 IOLockUnlock(registry_lock);return e;
}
int i9100_power_haptic_hold_get(i9100_power_haptic_client *client,i9100_power_haptic_hold *hold){return haptic_hold(client,hold,0);}
int i9100_power_haptic_hold_check(i9100_power_haptic_hold *hold){return hold?haptic_hold(hold->client,hold,1):I9100_SHARED_ARGUMENT;}
int i9100_power_haptic_hold_put(i9100_power_haptic_hold *hold){return hold?haptic_hold(hold->client,hold,2):I9100_SHARED_ARGUMENT;}
