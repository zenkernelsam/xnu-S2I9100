/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_modem_cp_native.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
struct i9100_modem_cp_native {
 i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;
 IOLock *lifecycle,*core_lock;
 struct i9100_modem_cp cp;
 uint32_t reserved;
};
static volatile uint32_t modem_cp_reserved;
static int take(IOLock *l){return l&&ml_get_interrupts_enabled()&&IOLockTryLock(l)?0:-1;}
static int core_take(void *p){return take(((i9100_modem_cp_native *)p)->core_lock);}
static void core_drop(void *p){IOLockUnlock(((i9100_modem_cp_native *)p)->core_lock);}
static uint64_t cp_now(void *)
{uint64_t n=0;absolutetime_to_nanoseconds(mach_absolute_time(),&n);return n;}
static int cp_delay(void *,uint32_t us)
{if(!ml_get_interrupts_enabled()||us>250U)return -1;IODelay(us);return 0;}
static int pins_qualified(i9100_modem_cp_native *n)
{
 /* Native-side qualification beyond the broker API: every claimed pin must
  * still record this object's lease generation as its broker owner. A copied
  * or stolen lease cannot reach this path because leases never leave the
  * object, but a revoked/foreign owner is caught here before each write. */
 struct i9100_gpio_broker *gpio=n->cp.gpio;
 struct i9100_gpio_lease *leases[3]={&n->cp.gpc1,&n->cp.gpx1,&n->cp.gpy4};
 if(!gpio||!gpio->io.lock)return I9100_CP_ARGUMENT;
 if(gpio->io.lock(gpio->io.cookie))return I9100_CP_BUSY;
 int ok=gpio->enabled;
 for(unsigned b=0;b<3U&&ok;b++){
  struct i9100_gpio_lease *l=leases[b];
  if(!l->active||l->broker!=gpio||l->generation>gpio->generation)ok=0;
  else for(unsigned p=0;p<8U;p++)if((l->mask&(1U<<p))&&gpio->banks[l->bank].owners[p]!=l->generation)ok=0;
 }
 gpio->io.unlock(gpio->io.cookie);
 return ok?0:I9100_CP_OWNER;
}
static void dispose(i9100_modem_cp_native *n)
{if(n->lifecycle)IOLockFree(n->lifecycle);if(n->core_lock)IOLockFree(n->core_lock);IOFree(n,sizeof(*n));}
int i9100_modem_cp_native_open(int enabled,i9100_peripheral_native *parent,int verified,
 i9100_modem_cp_native **out)
{
 if(!out)return I9100_CP_ARGUMENT;if(*out)return I9100_CP_BUSY;
 if(enabled!=1||!parent||verified!=1)return enabled!=1?I9100_CP_DISABLED:I9100_CP_ARGUMENT;
 if(!ml_get_interrupts_enabled())return I9100_CP_BUSY;
 if(__sync_lock_test_and_set(&modem_cp_reserved,1))return I9100_CP_BUSY;
 i9100_modem_cp_native *n=(i9100_modem_cp_native *)IOMalloc(sizeof(*n));
 if(!n){__sync_lock_release(&modem_cp_reserved);return I9100_CP_IO;}
 memset(n,0,sizeof(*n));n->parent=parent;n->reserved=1;
 n->lifecycle=IOLockAlloc();n->core_lock=IOLockAlloc();
 int e=0;
 if(!n->lifecycle||!n->core_lock)e=I9100_CP_IO;
 if(!e)e=i9100_peripheral_native_hold_get(parent,&n->parent_hold)?I9100_CP_IO:0;
 if(!e){
  struct i9100_gpio_broker *gpio=i9100_peripheral_native_gpio(parent);
  if(!gpio)e=I9100_CP_DISABLED;
  else{
   struct i9100_modem_cp_io io={n,core_take,core_drop,cp_now,cp_delay};
   e=i9100_modem_cp_init(&n->cp,1,gpio,&io,verified);
  }
 }
 if(e){
  /* A partial-open failure that still holds broker leases or configuration
   * must hand the live adapter back: the same owner then drives cleanup
   * through close until every resource is proven returned. Zero-resource
   * failures (claim refused, disabled, cold-state reject) free at once. */
  int retained=n->cp.initialized&&
   (n->cp.configured||n->cp.gpc1.active||n->cp.gpx1.active||n->cp.gpy4.active);
  if(retained){*out=n;return e;}
  if(n->parent_hold.owned)i9100_peripheral_native_hold_put(&n->parent_hold);
  __sync_lock_release(&modem_cp_reserved);n->reserved=0;dispose(n);return e;
 }
 *out=n;return 0;
}
static int entry(i9100_modem_cp_native *n,struct i9100_modem_cp **cp)
{
 if(!n||!n->reserved)return I9100_CP_ARGUMENT;
 int e=take(n->lifecycle);if(e)return I9100_CP_BUSY;
 if(n->cp.self!=(uintptr_t)&n->cp||!n->cp.initialized||n->cp.io.cookie!=(void *)n)
 {IOLockUnlock(n->lifecycle);return I9100_CP_ARGUMENT;}
 *cp=&n->cp;return 0;
}
static void leave(i9100_modem_cp_native *n){IOLockUnlock(n->lifecycle);}
int i9100_modem_cp_native_start(i9100_modem_cp_native *n,enum i9100_cp_op op)
{
 struct i9100_modem_cp *c;int e=entry(n,&c);if(e)return e;
 e=pins_qualified(n);if(!e)e=i9100_modem_cp_start(c,op);leave(n);return e;
}
int i9100_modem_cp_native_tick(i9100_modem_cp_native *n)
{
 struct i9100_modem_cp *c;int e=entry(n,&c);if(e)return e;
 e=c->op?pins_qualified(n):0;if(!e)e=i9100_modem_cp_tick(c);leave(n);return e;
}
int i9100_modem_cp_native_cancel(i9100_modem_cp_native *n)
{
 struct i9100_modem_cp *c;int e=entry(n,&c);if(e)return e;
 e=c->op?pins_qualified(n):0;if(!e)e=i9100_modem_cp_cancel(c);leave(n);return e;
}
int i9100_modem_cp_native_poll(i9100_modem_cp_native *n,struct i9100_modem_cp_pins *out)
{
 struct i9100_modem_cp *c;int e=entry(n,&c);if(e)return e;
 e=i9100_modem_cp_poll(c,out);leave(n);return e;
}
int i9100_modem_cp_native_status(i9100_modem_cp_native *n,struct i9100_modem_cp_status *out)
{
 struct i9100_modem_cp *c;int e=entry(n,&c);if(e)return e;
 e=i9100_modem_cp_status(c,out);leave(n);return e;
}
int i9100_modem_cp_native_close(i9100_modem_cp_native **owner)
{
 if(!owner||!*owner)return I9100_CP_ARGUMENT;
 i9100_modem_cp_native *n=*owner;
 if(!n->reserved||take(n->lifecycle))return I9100_CP_BUSY;
 if(n->cp.self!=(uintptr_t)&n->cp||!n->cp.initialized){leave(n);return I9100_CP_ARGUMENT;}
 /* No all-active preflight here: after a partial release some banks are
  * already returned, and the core's per-bank stop/release is re-entrant
  * and only touches pins this object still owns. */
 int e=i9100_modem_cp_close(&n->cp);
 if(e){leave(n);return e;}
 /* Success is only claimed when the core provably holds nothing: an
  * enabled/configured flag alone is never accepted as proof. */
 if(n->cp.gpc1.active||n->cp.gpx1.active||n->cp.gpy4.active){leave(n);return I9100_CP_STATE;}
 if(n->parent_hold.owned){e=i9100_peripheral_native_hold_put(&n->parent_hold);if(e){leave(n);return e;}}
 leave(n);
 __sync_lock_release(&modem_cp_reserved);n->reserved=0;dispose(n);*owner=0;return 0;
}
