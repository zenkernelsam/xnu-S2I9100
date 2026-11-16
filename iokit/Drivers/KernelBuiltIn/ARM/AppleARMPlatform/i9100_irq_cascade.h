/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_IRQ_CASCADE_H
#define I9100_IRQ_CASCADE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_cascade_kind {I9100_CASCADE_EINT=1,I9100_CASCADE_COMBINER=2};
enum i9100_cascade_error {I9100_CASCADE_ARGUMENT=-400,I9100_CASCADE_DISABLED=-401,
 I9100_CASCADE_BUSY=-402,I9100_CASCADE_OWNER=-403,I9100_CASCADE_RESERVED=-404,I9100_CASCADE_IO=-405};
struct i9100_cascade_io {
 void *cookie;
 uint32_t (*read32)(void *,unsigned region,unsigned off);
 void (*write32)(void *,unsigned region,unsigned off,uint32_t);
 uint32_t (*enter)(void *); void (*leave)(void *,uint32_t);
};
typedef int (*i9100_cascade_handler)(void *,unsigned child); /* zero after clearing source */
struct i9100_cascade_slot {
 i9100_cascade_handler handler;void *cookie;
 uint32_t token,busy,fault,initial,config,delivered;
 uintptr_t lease;uint32_t restoring;
};
struct i9100_irq_cascade {
 struct i9100_cascade_io io;
 struct i9100_cascade_slot slots[32];
 uint32_t allowed,enabled,kind,group,generation,initialized;
};
struct i9100_cascade_lease {struct i9100_irq_cascade *owner;uint32_t child,token,active;uintptr_t self;};
int i9100_cascade_init(struct i9100_irq_cascade *,const struct i9100_cascade_io *,enum i9100_cascade_kind,unsigned group,uint32_t allowed);
int i9100_cascade_enable(struct i9100_irq_cascade *,int);
int i9100_cascade_register(struct i9100_irq_cascade *,unsigned child,unsigned trigger,i9100_cascade_handler,void *,struct i9100_cascade_lease *);
int i9100_cascade_register_masked(struct i9100_irq_cascade *,unsigned,unsigned,i9100_cascade_handler,void *,struct i9100_cascade_lease *);
int i9100_cascade_mask(struct i9100_cascade_lease *);
int i9100_cascade_unmask(struct i9100_cascade_lease *);
int i9100_cascade_pending(struct i9100_cascade_lease *,unsigned *pending);
int i9100_cascade_enabled_children(struct i9100_irq_cascade *,uint32_t *mask);
int i9100_cascade_unregister(struct i9100_cascade_lease *);
int i9100_cascade_dispatch(struct i9100_irq_cascade *,uint32_t *unowned);
/* EINT trigger encodings: 0 low,1 high,2 falling,3 rising,4 both.
 * GPIO mux/lease and parent GIC lease are separate, acquired by the provider.
 * Combined sources have no generic acknowledge register: the handler clears
 * its own device status. This module never writes combiner/global reset.
 * A remaining asserted source after one handler call is masked, not spun on.
 * EINT uses only low3 bits of each4-bit trigger field. Register/restore/mask
 * are read back; failed restoration keeps the exact lease and rejects unmask.
 * A handler may intentionally mask its source for deferred work; an edge
 * latched while masked stays pending without being mislabeled as a storm.
 * All lease calls require the registered object address, not a copied token.
 * Dispatch leaves enabled unowned sources to the parent/provider. */
#ifdef __cplusplus
}
#endif
#endif
