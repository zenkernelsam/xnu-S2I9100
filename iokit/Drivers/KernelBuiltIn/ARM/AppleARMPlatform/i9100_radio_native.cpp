/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_radio_native.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
struct i9100_radio_native {
 i9100_peripheral_native *parent;i9100_radio_supply supply;IOLock *lock;
 i9100_radio_power power;i9100_native_transport_lease uart,sdio;
 i9100_radio_hold uart_hold,sdio_hold;uint32_t bound,closing,uart_epoch_used;int32_t error,cleanup_error;
};
static int control_lock(void *cookie)
{i9100_radio_native *r=(i9100_radio_native *)cookie;return !ml_get_interrupts_enabled()||!IOLockTryLock(r->lock)?-1:0;}
static void control_unlock(void *cookie){IOLockUnlock(((i9100_radio_native *)cookie)->lock);}
static uint64_t now_ns(void *){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
static int pause_us(void *,uint32_t us){if(!ml_get_interrupts_enabled()||us>100000U)return -1;IODelay(us);return 0;}
static int idle(void *cookie,enum i9100_radio_role role)
{i9100_radio_native *r=(i9100_radio_native *)cookie;return i9100_peripheral_native_transport_idle(r->parent,role==I9100_RADIO_WLAN?I9100_NATIVE_SDIO:I9100_NATIVE_UART);}
static int supply_get(void *cookie,unsigned *owned)
{i9100_radio_native *r=(i9100_radio_native *)cookie;return r->supply.get(r->supply.cookie,owned);}
static int supply_verify(void *cookie)
{i9100_radio_native *r=(i9100_radio_native *)cookie;return r->supply.verify(r->supply.cookie);}
static int supply_put(void *cookie)
{i9100_radio_native *r=(i9100_radio_native *)cookie;return r->supply.put(r->supply.cookie);}
static void dispose(i9100_radio_native *r){if(r->lock)IOLockFree(r->lock);IOFree(r,sizeof(*r));}
static bool output_valid(i9100_radio_native *r,const void *p,size_t n)
{
 uintptr_t a=(uintptr_t)r,b=(uintptr_t)p;
 return r&&p&&a<=~(uintptr_t)0-sizeof(*r)&&b<=~(uintptr_t)0-n&&!(a+sizeof(*r)>b&&b+n>a);
}
int i9100_radio_native_open(int enabled,i9100_peripheral_native *parent,const i9100_radio_supply *supply,int verified,i9100_radio_native **out)
{
 if(!out||(enabled!=0&&enabled!=1))return I9100_RADIO_ARGUMENT;if(*out)return I9100_RADIO_BUSY;
 if(!enabled)return I9100_RADIO_DISABLED;
 if(!parent||verified!=1||!supply||!supply->get||!supply->verify||!supply->put||!ml_get_interrupts_enabled())return I9100_RADIO_ARGUMENT;
 i9100_radio_native *r=(i9100_radio_native *)IOMalloc(sizeof(*r));if(!r)return I9100_RADIO_BUSY;
 memset(r,0,sizeof(*r));r->parent=parent;r->supply=*supply;r->lock=IOLockAlloc();
 if(!r->lock){dispose(r);return I9100_RADIO_BUSY;}
 int e=i9100_peripheral_native_radio_bind(parent,&r->uart,&r->sdio);
 if(e){dispose(r);return e;}r->bound=1;
 i9100_radio_io io={r,control_lock,control_unlock,now_ns,pause_us,idle,supply_get,supply_verify,supply_put};
 e=i9100_radio_power_init(&r->power,1,i9100_peripheral_native_gpio(parent),&io,verified);
 if(e){
  r->error=e;int cleanup=r->power.initialized?i9100_radio_power_close(&r->power):0;
  if(!cleanup)cleanup=i9100_peripheral_native_radio_unbind(parent,&r->uart,&r->sdio);
  if(cleanup){r->cleanup_error=cleanup;r->closing=1;*out=r;return e;}
  dispose(r);return e;
 }
 *out=r;return 0;
}
int i9100_radio_native_set_power(i9100_radio_native *r,enum i9100_radio_role role,int on)
{return !r?I9100_RADIO_ARGUMENT:on&&r->closing?I9100_RADIO_DISABLED:i9100_radio_set_power(&r->power,role,on);}
int i9100_radio_native_uart_open(i9100_radio_native *r,uint32_t clock,uint32_t baud,enum i9100_uart_fraction fraction,i9100_bt_io *out)
{
 if(!output_valid(r,out,sizeof(*out)))return I9100_RADIO_ARGUMENT;if(r->closing)return I9100_RADIO_DISABLED;
 if(r->uart.owned||r->uart_hold.owned)return I9100_RADIO_BUSY;
 if(r->power.generation[1]&&r->power.generation[1]<=r->uart_epoch_used)return I9100_RADIO_OWNER;
 int e=i9100_radio_hold_get(&r->power,I9100_RADIO_BT,&r->uart_hold);if(e)return e;
 r->uart_epoch_used=r->uart_hold.generation;
 e=i9100_peripheral_native_uart_open_owned(r->parent,&r->uart,clock,baud,fraction,1,out);
 if(e&&!r->uart.owned)r->cleanup_error=i9100_radio_hold_put(&r->uart_hold);
 r->error=e;return e;
}
int i9100_radio_native_uart_read(i9100_radio_native *r,uint8_t *bytes,unsigned capacity,unsigned *received)
{return output_valid(r,bytes,capacity)&&output_valid(r,received,sizeof(*received))?
 i9100_peripheral_native_uart_read_owned(&r->uart,bytes,capacity,received):I9100_RADIO_ARGUMENT;}
int i9100_radio_native_uart_validate_baud(i9100_radio_native *r,uint32_t generation,uint32_t expected,uint32_t target)
{
 int e=i9100_radio_native_verify(r,I9100_RADIO_BT,generation);if(e)return e;
 if(!r->uart.owned||!r->uart_hold.owned||r->uart_hold.generation!=generation)return I9100_RADIO_OWNER;
 return i9100_peripheral_native_uart_validate_baud_owned(&r->uart,expected,target);
}
int i9100_radio_native_uart_change_baud(i9100_radio_native *r,uint32_t generation,uint32_t expected,uint32_t target,int verified)
{
 int e=i9100_radio_native_verify(r,I9100_RADIO_BT,generation);if(e)return e;
 if(verified!=1||!r->uart.owned||!r->uart_hold.owned||r->uart_hold.generation!=generation)return I9100_RADIO_OWNER;
 return i9100_peripheral_native_uart_change_baud_owned(&r->uart,expected,target,verified);
}
int i9100_radio_native_sdio_open(i9100_radio_native *r,uint32_t clock,i9100_sdio **card,i9100_f2_io *out)
{
 if(!output_valid(r,card,sizeof(*card))||!output_valid(r,out,sizeof(*out)))return I9100_RADIO_ARGUMENT;if(r->closing)return I9100_RADIO_DISABLED;
 if(r->sdio.owned||r->sdio_hold.owned)return I9100_RADIO_BUSY;
 int e=i9100_radio_hold_get(&r->power,I9100_RADIO_WLAN,&r->sdio_hold);if(e)return e;
 e=i9100_peripheral_native_sdio_open_owned(r->parent,&r->sdio,clock,1,card,out);
 if(e&&!r->sdio.owned)r->cleanup_error=i9100_radio_hold_put(&r->sdio_hold);
 r->error=e;return e;
}
int i9100_radio_native_transport_close(i9100_radio_native *r,enum i9100_radio_role role)
{
 if(!r||(role!=I9100_RADIO_WLAN&&role!=I9100_RADIO_BT))return I9100_RADIO_ARGUMENT;
 i9100_native_transport_lease *lease=role==I9100_RADIO_WLAN?&r->sdio:&r->uart;
 i9100_radio_hold *hold=role==I9100_RADIO_WLAN?&r->sdio_hold:&r->uart_hold;
 int e=lease->owned?i9100_peripheral_native_transport_close(lease):0;
 if(!e&&hold->owned)e=i9100_radio_hold_put(hold);r->cleanup_error=e;return e;
}
int i9100_radio_native_borrow(i9100_radio_native *r,enum i9100_radio_role role,i9100_native_transport_hold *hold)
{
 if(!output_valid(r,hold,sizeof(*hold))||(role!=I9100_RADIO_WLAN&&role!=I9100_RADIO_BT))return I9100_RADIO_ARGUMENT;
 if(r->closing)return I9100_RADIO_DISABLED;
 return i9100_peripheral_native_transport_hold_get(role==I9100_RADIO_WLAN?&r->sdio:&r->uart,hold);
}
int i9100_radio_native_status(i9100_radio_native *r,i9100_radio_status *out)
{
 if(!output_valid(r,out,sizeof(*out)))return I9100_RADIO_ARGUMENT;
 int e=i9100_radio_power_status(&r->power,out);
 if(!e){out->native_bound=r->bound;out->native_closing=r->closing;out->native_uart_owned=r->uart.owned;out->native_sdio_owned=r->sdio.owned;
  out->native_error=r->error;out->native_cleanup_error=r->cleanup_error;out->native_uart_epoch_used=r->uart_epoch_used;}return e;
}
int i9100_radio_native_verify(i9100_radio_native *r,enum i9100_radio_role role,uint32_t generation)
{
 if(!r||(role!=I9100_RADIO_BT&&role!=I9100_RADIO_WLAN)||r->closing)return I9100_RADIO_OWNER;
 return i9100_radio_power_verify(&r->power,role,generation);
}
int i9100_radio_native_verify_uart_cleanup(i9100_radio_native *r,uint32_t generation)
{
 if(!r||!r->uart.owned||!r->uart_hold.owned||r->uart_hold.generation!=generation)return I9100_RADIO_OWNER;
 return i9100_radio_power_verify_held(&r->power,I9100_RADIO_BT,generation);
}
int i9100_radio_native_close(i9100_radio_native **owner)
{
 if(!owner||!*owner)return I9100_RADIO_ARGUMENT;i9100_radio_native *r=*owner;
 if(!output_valid(r,owner,sizeof(*owner)))return I9100_RADIO_ARGUMENT;r->closing=1;
 int e=r->power.initialized?i9100_radio_power_close(&r->power):0;
 if(!e&&r->bound){e=i9100_peripheral_native_radio_unbind(r->parent,&r->uart,&r->sdio);if(!e)r->bound=0;}
 r->cleanup_error=e;if(e)return e;dispose(r);*owner=0;return 0;
}
