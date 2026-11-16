/* SPDX-License-Identifier: BSD-2-Clause
 * Real native maps/GPIO/holds and GIC/cascade/router/IRQ adapter; IRQ write
 * semantics and worker hook API are stand-ins. SDIO state is cached injection,
 * not successful native F1 enumeration. No production test hook is installed. */
#include "i9100_bcm_irq.h"
#include "i9100_irq_router.h"
#include "i9100_native_parent_model.h"
struct worker_model {i9100_bcm_irq_ops ops;i9100_bcm_notify *token;unsigned notifiers,signals,detach_fail;};
static worker_model wm;
static i9100_bcm_worker *w(){return (i9100_bcm_worker *)&wm;}
int i9100_bcm_worker_notifier_get(i9100_bcm_worker *worker,i9100_bcm_notify *n)
{assert(worker==w()&&!n->owned);memset(n,0,sizeof(*n));n->worker=worker;n->self=(uintptr_t)n;n->owned=1;wm.notifiers++;return 0;}
int i9100_bcm_worker_notifier_put(i9100_bcm_notify *n)
{assert(!wm.ops.cookie&&wm.notifiers&&n->owned);n->owned=0;wm.notifiers--;return 0;}
int i9100_bcm_worker_signal(i9100_bcm_notify *n)
{assert(n->owned&&n->self==(uintptr_t)n&&n->worker==w());wm.signals++;return 0;}
int i9100_bcm_worker_irq_attach(i9100_bcm_worker *worker,const i9100_bcm_irq_ops *ops,i9100_bcm_notify *n)
{assert(worker==w()&&!wm.ops.cookie&&n->owned);wm.ops=*ops;wm.token=n;return 0;}
int i9100_bcm_worker_irq_detach(i9100_bcm_worker *worker,void *cookie)
{assert(worker==w()&&cookie==wm.ops.cookie);if(wm.detach_fail)return -90;int e=wm.ops.mask(cookie);if(!e)wm.ops={};return e;}
static unsigned fail_mask,fail_eint_write,eint_writes;
static void gic_write(void *cookie,unsigned off,uint32_t value)
{
 auto *c=(i9100_peripheral_native *)cookie;assert(c->irq_lock->held&&!interrupts);uint32_t *gic=memory[0x10490000U].data();
 if(off>=0x100U&&off<0x120U)gic[off/4U]|=value;
 else if(off>=0x180U&&off<0x1a0U){if(!fail_mask)gic[(off-0x80U)/4U]&=~value;}
 else gic[off/4U]=value;
}
static void fixture_eint_write(void *cookie,unsigned region,unsigned off,uint32_t value)
{
 auto *c=(i9100_peripheral_native *)cookie;assert(!region&&c->irq_lock->held&&!interrupts);eint_writes++;
 if(eint_writes==fail_eint_write)return;uint32_t *gpio=memory[0x11000000U].data();
 if(off>=0xf40U&&off<=0xf4cU)gpio[off/4U]&=~value;else gpio[off/4U]=value;
}
static i9100_native_transport_lease host;
static i9100_native_transport_hold session;
static i9100_peripheral_native *start()
{
 setup();wm={};fail_mask=fail_eint_write=eint_writes=0;i9100_peripheral_native *c=parent();
 c->irqs.io.write_dist=gic_write;c->eint.io.write32=fixture_eint_write;
 for(unsigned n=0;n<4U;n++)memory[0x11000000U][(0xf00U+n*4U)/4U]=255U;
 memory[0x11000000U][0xe08U/4U]=0xa9876543U;
 /* Cached SDIO-only injection for hold policy, never claim enumeration. */
 host={};session={};host.context=c;host.self=(uintptr_t)&host;host.kind=I9100_NATIVE_SDIO;host.generation=host.owned=1;
 c->sdio.live=c->sdio.generation=1;c->sdio.owner=&host;c->sdio.card.active=1;
 assert(!i9100_peripheral_native_transport_hold_get(&host,&session));return c;
}
static void finish(i9100_peripheral_native **c,i9100_bcm_irq **irq)
{
 assert(!i9100_bcm_irq_close(irq)&&!*irq&&!wm.notifiers&&!wm.ops.cookie&&(*c)->sdio.borrowers==1U);
 assert(!(memory[0x10490000U][0x108U/4U]&1U));assert(memory[0x11000000U][0xf08U/4U]&32U);
 assert(memory[0x11000000U][0xe08U/4U]==0xa9876543U);
 assert(!i9100_peripheral_native_transport_hold_put(&session));(*c)->sdio.live=(*c)->sdio.card.active=0;(*c)->sdio.owner=0;host.owned=0;
 assert(!i9100_peripheral_native_destroy(c)&&!active_allocations);
}
static struct i9100_bcm_irq_status status(i9100_bcm_irq *p)
{struct i9100_bcm_irq_status s;assert(!i9100_bcm_irq_status(p,&s));return s;}
static void normal()
{
 auto *c=start();i9100_bcm_irq *irq=nullptr;unsigned before=barriers;
 assert(i9100_bcm_irq_open(0,w(),&session,1,1,&irq)==I9100_PUMP_DISABLED&&!irq&&barriers==before);
 assert(i9100_bcm_irq_open(1,w(),&session,0,1,&irq)==I9100_PUMP_ARGUMENT&&!irq);
 assert(!i9100_bcm_irq_open(1,w(),&session,1,1,&irq)&&irq&&c->sdio.borrowers==2U&&wm.notifiers==1U);
 assert(!(memory[0x10490000U][0x108U/4U]&1U)&&(memory[0x11000000U][0xf08U/4U]&32U));
 assert((memory[0x11000000U][0xc40U/4U]>>20&15U)==15U);
 assert(!i9100_bcm_irq_enable(irq,1)&&wm.signals==1U&&!wm.ops.rearm(wm.ops.cookie));
 assert(memory[0x10490000U][0x108U/4U]&1U);assert(!(memory[0x11000000U][0xf08U/4U]&32U));
 memory[0x11000000U][0xf48U/4U]|=32U;interrupts=false;assert(i9100_irq_route(64U|(3U<<10))==1);interrupts=true;
 assert(wm.signals==2U&&status(irq).wakes==1U&&!status(irq).fault);
 assert(!(memory[0x10490000U][0x108U/4U]&1U)&&(memory[0x11000000U][0xf08U/4U]&32U));
 assert(!wm.ops.rearm(wm.ops.cookie));assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);finish(&c,&irq);
}
static void faults()
{
 auto *c=start();i9100_bcm_irq *irq=nullptr;memory[0x11000000U][0xf08U/4U]&=~4U;
 assert(i9100_bcm_irq_open(1,w(),&session,1,1,&irq)==I9100_CASCADE_BUSY&&!irq&&!wm.notifiers);
 memory[0x11000000U][0xf08U/4U]|=4U;assert(!i9100_bcm_irq_open(1,w(),&session,1,1,&irq));
 assert(!i9100_bcm_irq_enable(irq,1)&&!wm.ops.rearm(wm.ops.cookie));fail_mask=1;
 assert(i9100_bcm_irq_close(&irq)==I9100_IRQ_IO&&irq&&status(irq).notifier_owned&&status(irq).transport_hold);
 fail_mask=0;finish(&c,&irq);
 c=start();assert(!i9100_bcm_irq_open(1,w(),&session,1,1,&irq));wm.detach_fail=1;
 assert(i9100_bcm_irq_close(&irq)==-90&&irq&&status(irq).hook_owned&&status(irq).child_owned);wm.detach_fail=0;finish(&c,&irq);
 c=start();assert(!i9100_bcm_irq_open(1,w(),&session,1,1,&irq));assert(!i9100_bcm_irq_enable(irq,1)&&!wm.ops.rearm(wm.ops.cookie));
 memory[0x11000000U][0xf08U/4U]&=~4U;memory[0x11000000U][0xf48U/4U]|=4U;
 interrupts=false;assert(i9100_irq_route(64)==1);interrupts=true;
 assert(status(irq).fault&&status(irq).unowned==(1U<<18)&&(memory[0x11000000U][0xf48U/4U]&4U));
 memory[0x11000000U][0xf08U/4U]|=4U;finish(&c,&irq);
}
int main()
{normal();faults();puts("PASS actual native GPIO/holds+EINT21/GIC64/registry/router IRQ adapter: masked setup/reserved trigger bit/child-parent mask/deferred notify/rearm/foreign sibling rejection/no foreign ACK/failed mask-detach lifetime/restore before notifier-put; IRQ write and worker hook stand-ins, SDIO cached injection only");return 0;}
