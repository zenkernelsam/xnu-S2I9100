/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_IRQ_REGISTRY_H
#define I9100_IRQ_REGISTRY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_IRQ_SLOTS 64U
enum i9100_irq_error { I9100_IRQ_ARGUMENT=-320,I9100_IRQ_DISABLED=-321,
 I9100_IRQ_BUSY=-322,I9100_IRQ_OWNER=-323,I9100_IRQ_IO=-324,I9100_IRQ_EXHAUSTED=-325 };
struct i9100_irq_io {
 void *cookie;
 uint32_t (*read_dist)(void *,unsigned);
 void (*write_dist)(void *,unsigned,uint32_t);
 uint32_t (*enter)(void *); /* save/disable local IRQ, short spinlock */
 void (*leave)(void *,uint32_t saved);
};
typedef int (*i9100_irq_handler)(void *,uint32_t acknowledge); /* 0=source handled/cleared; <0=mask */
struct i9100_irq_slot {
 i9100_irq_handler handler;void *cookie;
 uint32_t irq,token,busy,enabled,fault,delivered,priority,target,config;
};
struct i9100_irq_registry {
 struct i9100_irq_io io;struct i9100_irq_slot slots[I9100_IRQ_SLOTS];
 uint32_t allowed[8],enabled,generation;
};
struct i9100_irq_lease {struct i9100_irq_registry *registry;uint32_t slot,token,active;};
int i9100_irq_registry_init(struct i9100_irq_registry *,const struct i9100_irq_io *,const uint32_t allowed[8]);
int i9100_irq_registry_enable(struct i9100_irq_registry *,int);
int i9100_irq_register(struct i9100_irq_registry *,unsigned irq,int edge,i9100_irq_handler,void *,struct i9100_irq_lease *);
int i9100_irq_register_masked(struct i9100_irq_registry *,unsigned irq,int edge,i9100_irq_handler,void *,struct i9100_irq_lease *);
int i9100_irq_mask(struct i9100_irq_lease *);
int i9100_irq_unmask(struct i9100_irq_lease *);
int i9100_irq_unregister(struct i9100_irq_lease *);
int i9100_irq_dispatch(struct i9100_irq_registry *,uint32_t acknowledge);
/* No GIC reset, no SGI/PPI changes and no EOI: caller retains the complete
 * acknowledge token and performs exactly one EOI. Allowed IRQs come from the
 * board profile, with active mainline sources excluded. Objects are zeroed. */
#ifdef __cplusplus
}
#endif
#endif
