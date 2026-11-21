/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_board_native.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
uint32_t i9100_camera_mmio_read(volatile uint8_t *,unsigned);
void i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
}
struct i9100_mfc_board {
 uintptr_t self;i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;
 IOLock *lifecycle,*power_lock;i9100_mfc_host_lease host;i9100_irq_lease irq;
 IOMemoryDescriptor *memory[MP_REGIONS];IOMemoryMap *maps[MP_REGIONS];volatile uint8_t *regs[MP_REGIONS];
 i9100_mfc_power power;i9100_mfc_supply_lease *borrower;uint32_t closing,ready,attempted;int error,cleanup_error;
};
static bool thread(){return ml_get_interrupts_enabled()&&!ml_at_interrupt_context();}
static int take(IOLock *l){return l&&thread()&&IOLockTryLock(l)?0:-1;}
static bool outside(i9100_mfc_board *b,const void *p,size_t bytes)
{uintptr_t a=(uintptr_t)p,lo=(uintptr_t)b;return p&&bytes&&a<=~(uintptr_t)0-bytes&&!(a<lo+sizeof(*b)&&lo<a+bytes);}
static uint32_t read32(void *p,unsigned region,unsigned off)
{i9100_mfc_board *b=(i9100_mfc_board *)p;return region<MP_REGIONS&&b->regs[region]?i9100_camera_mmio_read(b->regs[region],off):0xffffffffU;}
static void write32(void *p,unsigned region,unsigned off,uint32_t value)
{i9100_mfc_board *b=(i9100_mfc_board *)p;if(region<MP_REGIONS&&b->regs[region])i9100_camera_mmio_write(b->regs[region],off,value);}
static int power_take(void *p){return take(((i9100_mfc_board *)p)->power_lock);}
static void power_drop(void *p){IOLockUnlock(((i9100_mfc_board *)p)->power_lock);}
static uint64_t now(void *){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
static int delay(void *,uint32_t us){if(!thread()||us>1000U)return -1;IODelay(us);return 0;}
static void barrier(void *){OSSynchronizeIO();}
static int masked_irq(void *p,uint32_t){(void)p;return -1;}
static int irq_exact(i9100_mfc_board *b)
{
 i9100_irq_lease *l=&b->irq;i9100_irq_registry *r=l->registry;
 if(!r||!l->active||l->self!=(uintptr_t)l||l->slot>=I9100_IRQ_SLOTS)return -1;
 uint32_t saved=r->io.enter(r->io.cookie);i9100_irq_slot *s=&r->slots[l->slot];
 bool ok=r->enabled&&s->irq==126U&&s->token==l->token&&s->lease==(uintptr_t)l&&s->ready&&!s->restoring&&!s->enabled&&!s->fault&&
  !(r->io.read_dist(r->io.cookie,0x10c)&(1U<<30))&&!(r->io.read_dist(r->io.cookie,0x30c)&(1U<<30))&&
  ((r->io.read_dist(r->io.cookie,0x47c)>>16)&255U)==0x80U&&
  ((r->io.read_dist(r->io.cookie,0x87c)>>16)&255U)==1U&&!(r->io.read_dist(r->io.cookie,0xc1c)&(2U<<28));
 r->io.leave(r->io.cookie,saved);return ok?0:-1;
}
static int exclusive(void *p)
{i9100_mfc_board *b=(i9100_mfc_board *)p;return i9100_mfc_host_check(&b->host)||irq_exact(b)?-1:0;}
static int consumers_idle(void *p)
{i9100_mfc_board *b=(i9100_mfc_board *)p;return b->borrower||i9100_mfc_host_has_child(&b->host)!=0?-1:0;}
static int stop_state(i9100_mfc_board *b)
{
 uint32_t gate=read32(b,MP_CMU,0xc928),config=read32(b,MP_PMU,0x3c40),status=read32(b,MP_PMU,0x3c44);
 if(gate==0xffffffffU||config==0xffffffffU||status==0xffffffffU||(gate&7U)!=7U||(config&7U)!=7U||(status&7U)!=7U)return I9100_MP_OWNER;
 uint32_t reset=read32(b,MP_MFC,0),mc=read32(b,MP_MFC,0x510);
 if(reset==0xffffffffU||mc==0xffffffffU)return I9100_MP_IO;
 if(reset&1U||mc&3U||read32(b,MP_MFC,0x30)||read32(b,MP_MFC,0x44)||read32(b,MP_MFC,8))return I9100_MP_BUSY;
 for(unsigned unit=MP_MMU_L;unit<=MP_MMU_R;unit++)if(read32(b,unit,0)||read32(b,unit,0x18))return I9100_MP_BUSY;
 return 0;
}
static bool lease_exact(i9100_mfc_board *b,const i9100_mfc_supply_lease *l)
{return b->borrower==l&&l&&l->self==(uintptr_t)l&&l->provider==(uintptr_t)b&&l->owned==1U&&l->generation==b->power.generation;}
static int supply_get(void *p,i9100_mfc_supply_lease *l)
{
 i9100_mfc_board *b=(i9100_mfc_board *)p;
 if(!b||b->self!=(uintptr_t)b||!outside(b,l,sizeof(*l)))return I9100_MP_ARGUMENT;if(take(b->lifecycle))return I9100_MP_BUSY;
 int e=b->closing||!b->ready||b->error?I9100_MP_DISABLED:b->borrower?I9100_MP_BUSY:0;
 if(!e&&(l->owned||l->self))e=I9100_MP_ARGUMENT;
 if(!e&&i9100_mfc_host_has_child(&b->host)!=1)e=I9100_MP_OWNER;
 if(!e)e=i9100_mfc_power_validate(&b->power);
 if(!e){memset(l,0,sizeof(*l));l->self=(uintptr_t)l;l->provider=(uintptr_t)b;l->generation=b->power.generation;l->owned=1;b->borrower=l;}
 IOLockUnlock(b->lifecycle);return e;
}
static int supply_check(void *p,const i9100_mfc_supply_lease *l)
{
 i9100_mfc_board *b=(i9100_mfc_board *)p;if(!b||b->self!=(uintptr_t)b)return I9100_MP_OWNER;
 if(take(b->lifecycle))return I9100_MP_BUSY;int e=!lease_exact(b,l)||b->closing?I9100_MP_OWNER:0;
 if(!e){e=i9100_mfc_power_validate(&b->power);if(e&&!b->error)b->error=e;}
 IOLockUnlock(b->lifecycle);return e;
}
static int supply_put(void *p,i9100_mfc_supply_lease *l)
{
 i9100_mfc_board *b=(i9100_mfc_board *)p;if(!b||b->self!=(uintptr_t)b)return I9100_MP_OWNER;
 if(take(b->lifecycle))return I9100_MP_BUSY;int e=!lease_exact(b,l)?I9100_MP_OWNER:stop_state(b);
 if(!e){b->borrower=0;l->owned=0;}IOLockUnlock(b->lifecycle);return e;
}
static int cleanup(i9100_mfc_board *b)
{
 if(b->borrower||(b->host.owned&&i9100_mfc_host_has_child(&b->host)!=0))return I9100_MP_BUSY;
 if(b->host.owned){int e=i9100_mfc_host_begin_close(&b->host);if(e)return e;}
 if(b->power.owned){int e=i9100_mfc_power_release(&b->power);if(e)return e;}
 if(b->irq.active){int e=i9100_irq_unregister(&b->irq);if(e)return e;}
 if(b->host.owned){int e=i9100_mfc_host_release(&b->host,1);if(e)return e;}
 for(unsigned n=0;n<MP_REGIONS;n++){
  if(b->maps[n]){b->maps[n]->release();b->maps[n]=0;b->regs[n]=0;}
  if(b->memory[n]){b->memory[n]->release();b->memory[n]=0;}
 }
 if(b->parent_hold.owned){int e=i9100_peripheral_native_hold_put(&b->parent_hold);if(e)return e;}return 0;
}
static void dispose(i9100_mfc_board *b)
{if(b->lifecycle)IOLockFree(b->lifecycle);if(b->power_lock)IOLockFree(b->power_lock);IOFree(b,sizeof(*b));}
int i9100_mfc_board_open(int enabled,i9100_peripheral_native *parent,const i9100_mfc_power_config *cfg,i9100_mfc_board **out)
{
 if(!enabled)return I9100_MP_DISABLED;
 if(enabled!=1||!parent||!cfg||!out||*out||!thread()||cfg->board_data_reviewed!=1U||cfg->xxti_hz!=24000000U||
    cfg->maximum_clock_hz<10000000U||cfg->maximum_clock_hz>200000000U)return I9100_MP_ARGUMENT;
 i9100_mfc_board *b=(i9100_mfc_board *)IOMalloc(sizeof(*b));if(!b)return I9100_MP_IO;
 memset(b,0,sizeof(*b));b->self=(uintptr_t)b;b->parent=parent;
 int e=i9100_peripheral_native_hold_get(parent,&b->parent_hold);if(e){dispose(b);return e;}*out=b;
 b->lifecycle=IOLockAlloc();b->power_lock=IOLockAlloc();if(!b->lifecycle||!b->power_lock){e=I9100_MP_IO;goto fail;}
 e=i9100_mfc_host_acquire(0,&b->host);if(e)goto fail;
 {
  const uint32_t addresses[MP_REGIONS]={0x10030000U,0x10020000U,0x10000000U,0x13400000U,0x13620000U,0x13630000U,0x10490000U};
  const unsigned order[MP_REGIONS]={MP_CMU,MP_PMU,MP_CHIP,MP_GIC,MP_MFC,MP_MMU_L,MP_MMU_R};
  for(unsigned k=0;k<MP_REGIONS;k++){
   unsigned n=order[k];uint32_t bytes=n==MP_CMU?131072U:n==MP_PMU?16384U:n==MP_MFC?65536U:4096U;
   b->memory[n]=IOMemoryDescriptor::withPhysicalAddress(addresses[n],bytes,kIODirectionInOut);
   b->maps[n]=b->memory[n]?b->memory[n]->map(kIOMapInhibitCache):0;b->regs[n]=b->maps[n]?(volatile uint8_t *)b->maps[n]->getAddress():0;
   if(!b->regs[n]){e=I9100_MP_IO;goto fail;}
  }
  uint32_t id=read32(b,MP_CHIP,0);if(id==0xffffffffU||(id&0xfffe0000U)!=(0x43210000U&0xfffe0000U)){e=I9100_MP_OWNER;goto fail;}
 }
 e=i9100_irq_register_masked(i9100_peripheral_native_irqs(parent),126,0,masked_irq,b,&b->irq);if(e)goto fail;
 {
  i9100_mfc_power_io io={b,read32,write32,power_take,power_drop,now,delay,barrier,exclusive,consumers_idle};
  e=i9100_mfc_power_init(&b->power,&io,cfg);if(!e)e=i9100_mfc_power_enable(&b->power,1);if(e)goto fail;
  b->attempted=1;e=i9100_mfc_power_acquire(&b->power);if(e)goto fail;
 }
 b->ready=1;return 0;
fail:b->error=e;b->closing=1;b->cleanup_error=cleanup(b);
 if(!b->cleanup_error){dispose(b);*out=0;}return e;
}
int i9100_mfc_board_supply(i9100_mfc_board *b,i9100_mfc_supply *out)
{
 if(!b||b->self!=(uintptr_t)b||!outside(b,out,sizeof(*out)))return I9100_MP_ARGUMENT;if(take(b->lifecycle))return I9100_MP_BUSY;
 int e=b->closing||!b->ready||b->error?I9100_MP_DISABLED:i9100_mfc_power_validate(&b->power);
 if(!e){memset(out,0,sizeof(*out));out->cookie=b;out->acquire=supply_get;out->validate=supply_check;out->release=supply_put;out->host=&b->host;}
 IOLockUnlock(b->lifecycle);return e;
}
int i9100_mfc_board_status(i9100_mfc_board *b,struct i9100_mfc_board_status *out)
{
 if(!b||b->self!=(uintptr_t)b||!outside(b,out,sizeof(*out)))return I9100_MP_ARGUMENT;if(take(b->lifecycle))return I9100_MP_BUSY;
 memset(out,0,sizeof(*out));out->enabled=b->power.enabled;out->owned=b->power.owned;
 out->ready=b->ready&&b->power.ready&&!b->power.fault&&!b->closing&&!b->error;
 out->borrowed=b->borrower!=0;out->generation=b->power.generation;out->parent_hz=b->power.parent_hz;out->clock_hz=b->power.clock_hz;
 out->divider=b->power.divider;out->reset=b->power.confirm_reset;out->mc=b->power.confirm_mc;out->error=b->error;out->cleanup_error=b->cleanup_error;
 memcpy(out->before,b->power.before,sizeof(out->before));memcpy(out->after,b->power.after,sizeof(out->after));IOLockUnlock(b->lifecycle);return 0;
}
int i9100_mfc_board_close(i9100_mfc_board **owner)
{
 if(!owner||!*owner||(*owner)->self!=(uintptr_t)*owner||!thread())return I9100_MP_ARGUMENT;i9100_mfc_board *b=*owner;
 if(b->lifecycle&&take(b->lifecycle))return I9100_MP_BUSY;
 if(b->borrower||(b->host.owned&&i9100_mfc_host_has_child(&b->host)!=0)){if(b->lifecycle)IOLockUnlock(b->lifecycle);return I9100_MP_BUSY;}
 b->closing=1;int e=cleanup(b);b->cleanup_error=e;if(b->lifecycle)IOLockUnlock(b->lifecycle);
 if(e)return e;*owner=0;dispose(b);return 0;
}
