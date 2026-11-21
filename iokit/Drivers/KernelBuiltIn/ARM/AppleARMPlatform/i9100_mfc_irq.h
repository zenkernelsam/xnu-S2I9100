/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_IRQ_H
#define I9100_MFC_IRQ_H
#include "i9100_mfc_board_native.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct i9100_mfc_irq i9100_mfc_irq;
struct i9100_mfc_irq;
typedef struct i9100_mfc_irq_notify i9100_mfc_irq_notify;
struct i9100_mfc_irq_notify {void *cookie;void (*signal)(void *);};
struct i9100_mfc_irq_status {
 uint32_t bound,armed,delivered,spurious,generation,enabled,active,dispatch;int error,cleanup_error;
};
unsigned long i9100_mfc_irq_storage(void);
uintptr_t i9100_mfc_irq_token_id(struct i9100_mfc_irq *);
int i9100_mfc_irq_open(int enabled,struct i9100_mfc_board *,void *storage,unsigned long bytes,struct i9100_mfc_irq **);
int i9100_mfc_irq_bind(struct i9100_mfc_irq *,const struct i9100_mfc_irq_notify *);
int i9100_mfc_irq_arm(struct i9100_mfc_irq *);
int i9100_mfc_irq_disarm(struct i9100_mfc_irq *);
/* Faulted-slot owner repair: the exact bound token proves mask+dispatch/EOI
 * quiet so the latched registry mismatch resolves; the binding stays, the
 * recorded diagnostics stay, and normal issue still refuses while faulted. */
int i9100_mfc_irq_repair(struct i9100_mfc_irq *);
int i9100_mfc_irq_ack(struct i9100_mfc_irq *);
int i9100_mfc_irq_status(struct i9100_mfc_irq *,struct i9100_mfc_irq_status *);
/* Consume the board's ISR-published persistent domain fault, keyed to this
 * binding's exact token self+generation: returns 0 and sets *code to the
 * published fault while an accusation is pending, or leaves it 0. The read
 * is non-destructive — the record outlives resource restoration, repeated
 * queries and merged notifies until the binding's own release/cleanup. */
int i9100_mfc_irq_fault(struct i9100_mfc_irq *,int *code);
int i9100_mfc_irq_close(struct i9100_mfc_irq **);
/* Exact single notification owner for the board-held masked IRQ126 lease.
 * Acquires the board irq-owner token so only this controller may bind a
 * notify-only sink and mask/unmask the line; no second registry handler, no
 * slot mutation and never IAR/EOI. The ISR sink runs in interrupt context
 * and may only mark its own counters and call the bound signal callback.
 * arm/disarm serialize through the board lifecycle and re-verify the exact
 * slot/lease/handler before touching GIC enable state. Close first disarms,
 * then releases the board token; a line that will not stay masked retains
 * the token and reports the retained error. Delivered/spurious counters are
 * model evidence of dispatch only, not proof of hardware interrupt
 * delivery. */
#ifdef __cplusplus
}
#endif
#endif
