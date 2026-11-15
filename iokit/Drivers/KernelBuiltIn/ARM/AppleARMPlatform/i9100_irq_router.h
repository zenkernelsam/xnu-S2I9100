/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_IRQ_ROUTER_H
#define I9100_IRQ_ROUTER_H
#include "i9100_irq_registry.h"
#ifdef __cplusplus
extern "C" {
#endif
int i9100_irq_router_attach(struct i9100_irq_registry *);
int i9100_irq_router_detach(struct i9100_irq_registry *);
int i9100_irq_route(uint32_t complete_iar_token);
/* Default NULL. Router accepts the caller's existing IAR token, never reads
 * IAR or writes EOI. Last detach is refused during dispatch; the registry
 * and its owner must remain live until detach succeeds. Global short spin
 * lock with local IRQ masking supports multiple callers/cores. */
#ifdef __cplusplus
}
#endif
#endif
