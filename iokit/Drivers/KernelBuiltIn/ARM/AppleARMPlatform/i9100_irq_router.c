/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_irq_router.h"
#include <machine/machine_routines.h>
static volatile uint32_t router_lock;
static struct i9100_irq_registry *active;
static uint32_t references,dispatching;
static int enter(void)
{
 int previous=ml_set_interrupts_enabled(0);
 while(__sync_lock_test_and_set(&router_lock,1U)){}
 return previous;
}
static void leave(int previous)
{__sync_lock_release(&router_lock);ml_set_interrupts_enabled(previous);}
int i9100_irq_router_attach(struct i9100_irq_registry *r)
{
 if(!r||!r->enabled)return I9100_IRQ_ARGUMENT;int previous=enter(),e=0;
 if(active&&active!=r)e=I9100_IRQ_BUSY;
 else if(references==0xffffffffU)e=I9100_IRQ_EXHAUSTED;
 else{active=r;references++;}leave(previous);return e;
}
int i9100_irq_router_detach(struct i9100_irq_registry *r)
{
 int previous=enter(),e=0;
 if(!r||active!=r||!references)e=I9100_IRQ_OWNER;
 else if(references==1U&&dispatching)e=I9100_IRQ_BUSY;
 else if(!--references)active=0;
 leave(previous);return e;
}
int i9100_irq_route(uint32_t token)
{
 unsigned irq=token&1023U;if(irq<32U||irq>=256U)return 0;
 int previous=enter();struct i9100_irq_registry *r=active;
 if(!r){leave(previous);return 0;}
 if(dispatching==0xffffffffU){leave(previous);return I9100_IRQ_EXHAUSTED;}
 dispatching++;leave(previous);int result=i9100_irq_dispatch(r,token);
 previous=enter();dispatching--;leave(previous);return result;
}
