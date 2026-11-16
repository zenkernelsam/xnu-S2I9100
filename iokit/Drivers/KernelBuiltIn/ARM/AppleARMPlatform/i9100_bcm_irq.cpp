/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_irq.h"
#include "i9100_irq_router.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <machine/machine_routines.h>
#include <string.h>
struct i9100_bcm_irq {
 i9100_bcm_worker *worker;i9100_peripheral_native *native;i9100_irq_registry *registry;i9100_irq_cascade *cascade;
 IOSimpleLock *guard;i9100_irq_lease parent;i9100_cascade_lease child;i9100_gpio_lease pins;
 i9100_bcm_notify notify;i9100_native_transport_hold hold;
 uint32_t enabled,closing,fault,wakes,unowned,hook,router,pin_changed;int32_t error,cleanup_error;
};
static IOInterruptState enter(i9100_bcm_irq *p){return IOSimpleLockLockDisableInterrupt(p->guard);}
static void leave(i9100_bcm_irq *p,IOInterruptState s){IOSimpleLockUnlockEnableInterrupt(p->guard,s);}
static int mask_sources(void *cookie)
{
 i9100_bcm_irq *p=(i9100_bcm_irq *)cookie;int e=p->child.active?i9100_cascade_mask(&p->child):0;
 int parent=p->parent.active?i9100_irq_mask(&p->parent):0;return e?e:parent;
}
static int rearm(void *cookie)
{
 i9100_bcm_irq *p=(i9100_bcm_irq *)cookie;IOInterruptState state=enter(p);
 bool on=p->enabled&&!p->closing&&!p->fault;leave(p,state);if(!on)return 1;
 int e=i9100_cascade_unmask(&p->child);if(!e)e=i9100_irq_unmask(&p->parent);
 if(e){(void)mask_sources(p);state=enter(p);p->fault=1;p->error=e;leave(p,state);}return e;
}
static int child(void *cookie,unsigned line)
{
 i9100_bcm_irq *p=(i9100_bcm_irq *)cookie;if(line!=21U)return I9100_CASCADE_OWNER;
 int e=mask_sources(p);IOInterruptState state=enter(p);
 bool wake=p->enabled&&!p->closing&&!p->fault;if(p->wakes!=0xffffffffU)p->wakes++;
 if(e){p->fault=1;p->error=e;}leave(p,state);
 if(!e&&wake)e=i9100_bcm_worker_signal(&p->notify);return e;
}
static int parent(void *cookie,uint32_t token)
{
 i9100_bcm_irq *p=(i9100_bcm_irq *)cookie;if((token&1023U)!=64U)return I9100_IRQ_OWNER;
 uint32_t unowned=0;int e=i9100_cascade_dispatch(p->cascade,&unowned);
 if(unowned||e<0){(void)mask_sources(p);IOInterruptState state=enter(p);p->fault=1;p->unowned|=unowned;p->error=e<0?e:I9100_CASCADE_OWNER;leave(p,state);return p->error;}
 return 0;
}
static bool separate(i9100_bcm_irq *p,const void *out,size_t bytes)
{uintptr_t a=(uintptr_t)p,b=(uintptr_t)out;return p&&out&&a<=~(uintptr_t)0-sizeof(*p)&&b<=~(uintptr_t)0-bytes&&!(a+sizeof(*p)>b&&b+bytes>a);}
static void dispose(i9100_bcm_irq *p){if(p->guard)IOSimpleLockFree(p->guard);IOFree(p,sizeof(*p));}
static int close(i9100_bcm_irq *p)
{
 IOInterruptState state=enter(p);p->closing=1;p->enabled=0;leave(p,state);int e=mask_sources(p);if(e)return e;
 if(p->hook){e=i9100_bcm_worker_irq_detach(p->worker,p);if(e)return e;p->hook=0;}
 if(p->parent.active){e=i9100_irq_unregister(&p->parent);if(e)return e;}
 if(p->router){e=i9100_irq_router_detach(p->registry);if(e)return e;p->router=0;}
 if(p->child.active){e=i9100_cascade_unregister(&p->child);if(e)return e;}
 if(p->pins.active){e=i9100_gpio_release(&p->pins,p->pin_changed?I9100_GPIO_RESTORE:I9100_GPIO_KEEP);if(e)return e;}
 if(p->notify.owned){e=i9100_bcm_worker_notifier_put(&p->notify);if(e)return e;}
 if(p->hold.owned){e=i9100_peripheral_native_transport_hold_put(&p->hold);if(e)return e;}
 return 0;
}
int i9100_bcm_irq_open(int enabled,i9100_bcm_worker *worker,i9100_native_transport_hold *session,int dispatch,int eur,i9100_bcm_irq **out)
{
 if(!out||(enabled!=0&&enabled!=1))return I9100_PUMP_ARGUMENT;if(*out)return I9100_PUMP_BUSY;if(!enabled)return I9100_PUMP_DISABLED;
 if(!worker||!session||!session->owned||session->self!=(uintptr_t)session||session->kind!=I9100_NATIVE_SDIO||
    !session->lease||!session->lease->context||dispatch!=1||eur!=1||!ml_get_interrupts_enabled())return I9100_PUMP_ARGUMENT;
 int e=i9100_peripheral_native_transport_hold_check(session);if(e)return e;
 i9100_bcm_irq *p=(i9100_bcm_irq *)IOMalloc(sizeof(*p));if(!p)return I9100_PUMP_IO;
 memset(p,0,sizeof(*p));p->guard=IOSimpleLockAlloc();if(!p->guard){dispose(p);return I9100_PUMP_IO;}
 p->worker=worker;p->native=session->lease->context;p->registry=i9100_peripheral_native_irqs(p->native);p->cascade=i9100_peripheral_native_eint(p->native);
 e=i9100_peripheral_native_transport_hold_get(session->lease,&p->hold);
 if(!e){uint32_t sources=0;e=i9100_cascade_enabled_children(p->cascade,&sources);if(!e&&(sources&0xffff0000U))e=I9100_CASCADE_BUSY;}
 if(!e){uint32_t saved=p->registry->io.enter(p->registry->io.cookie);
  if(p->registry->io.read_dist(p->registry->io.cookie,0x108U)&1U)e=I9100_IRQ_BUSY;
  p->registry->io.leave(p->registry->io.cookie,saved);}
 if(!e)e=i9100_bcm_worker_notifier_get(worker,&p->notify);
 if(!e)e=i9100_gpio_claim(i9100_peripheral_native_gpio(p->native),I9100_GPX2,32U,&p->pins);
 if(!e){unsigned mode=(p->pins.saved[0]>>20)&15U;if(mode!=0U&&mode!=15U)e=I9100_GPIO_BUSY;}
 if(!e){p->pin_changed=1;e=i9100_gpio_pull(&p->pins,5,1);if(!e)e=i9100_gpio_function(&p->pins,5,15);}
 if(!e)e=i9100_cascade_register_masked(p->cascade,21,2,child,p,&p->child);
 if(!e)e=i9100_irq_register_masked(p->registry,64,0,parent,p,&p->parent);
 if(!e){e=i9100_irq_router_attach(p->registry);if(!e)p->router=1;}
 if(!e){i9100_bcm_irq_ops ops={p,rearm,mask_sources};e=i9100_bcm_worker_irq_attach(worker,&ops,&p->notify);if(!e)p->hook=1;}
 if(e){p->error=e;p->cleanup_error=close(p);if(p->cleanup_error)*out=p;else dispose(p);return e;}
 *out=p;return 0;
}
int i9100_bcm_irq_enable(i9100_bcm_irq *p,int enabled)
{
 if(!p||(enabled!=0&&enabled!=1)||!ml_get_interrupts_enabled())return I9100_PUMP_ARGUMENT;
 IOInterruptState state=enter(p);if(p->closing||p->fault){leave(p,state);return I9100_PUMP_DISABLED;}
 p->enabled=(uint32_t)enabled;leave(p,state);return enabled?i9100_bcm_worker_signal(&p->notify):mask_sources(p);
}
int i9100_bcm_irq_status(i9100_bcm_irq *p,struct i9100_bcm_irq_status *out)
{
 if(!separate(p,out,sizeof(*out))||!ml_get_interrupts_enabled())return I9100_PUMP_ARGUMENT;
 IOInterruptState state=enter(p);struct i9100_bcm_irq_status value={p->enabled,p->closing,p->fault,p->wakes,p->unowned,
  p->parent.active,p->child.active,p->pins.active,p->notify.owned,p->hold.owned,p->hook,p->error,p->cleanup_error};
 leave(p,state);*out=value;return 0;
}
int i9100_bcm_irq_close(i9100_bcm_irq **owner)
{
 if(!owner||!*owner||!ml_get_interrupts_enabled())return I9100_PUMP_ARGUMENT;i9100_bcm_irq *p=*owner;
 if(!separate(p,owner,sizeof(*owner)))return I9100_PUMP_ARGUMENT;int e=close(p);p->cleanup_error=e;
 if(!e){dispose(p);*owner=0;}return e;
}
