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
struct read_context {OSObject *owner;i9100_power_hw *hw;uint32_t generation,refs,open;};
static volatile uint32_t state;
static IOLock *bus_lock[2],*registry_lock;
static i9100_power_bus_ticket *bus_owner[2];
static read_context *published;
static OSObject *volatile published_owner,*volatile withdrawn_owner;
static uint32_t generation;
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
int i9100_power_shared_publish(void *object,i9100_power_hw *hw)
{
 if(!object||!hw||!hw->cookie||!hw->read32||!hw->write32||!hw->delay_us||!hw->enter||!hw->leave||!thread_ok())return I9100_SHARED_ARGUMENT;
 if(mode()!=READY)return I9100_SHARED_DISABLED;int e=lock_bounded(registry_lock);if(e)return e;
 if(published||generation==0xffffffffU){IOLockUnlock(registry_lock);return I9100_SHARED_BUSY;}
 read_context *ctx=(read_context *)IOMalloc(sizeof(*ctx));if(!ctx){IOLockUnlock(registry_lock);return I9100_SHARED_IO;}
 ctx->owner=(OSObject *)object;ctx->hw=hw;ctx->generation=++generation;ctx->refs=ctx->open=1;ctx->owner->retain();published=ctx;
 __sync_lock_test_and_set(&withdrawn_owner,(OSObject *)0);__sync_lock_test_and_set(&published_owner,ctx->owner);
 IOLockUnlock(registry_lock);return 0;
}
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
