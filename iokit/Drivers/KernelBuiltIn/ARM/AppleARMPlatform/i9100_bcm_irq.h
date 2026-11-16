/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_IRQ_H
#define I9100_BCM_IRQ_H
#include "i9100_bcm_worker.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_bcm_irq;
struct i9100_bcm_irq_status {uint32_t enabled,closing,fault,wakes,unowned,parent_owned,child_owned,pins_owned,notifier_owned,transport_hold,hook_owned;int32_t error,cleanup_error;};
int i9100_bcm_irq_open(int enabled,struct i9100_bcm_worker *,struct i9100_native_transport_hold *session,
 int verified_iar_dispatch,int eur_u1_verified,struct i9100_bcm_irq **);
int i9100_bcm_irq_enable(struct i9100_bcm_irq *,int enabled);
int i9100_bcm_irq_status(struct i9100_bcm_irq *,struct i9100_bcm_irq_status *);
int i9100_bcm_irq_close(struct i9100_bcm_irq **);
/* Default OFF. EUR U1 GPX2.5 AFf, EINT21 falling, GIC64 level parent.
 * Uses actual native maps/brokers/cascade/registry and worker notifier/hook.
 * Requires a live native session and an actually installed one-IAR/one-EOI
 * dispatcher bridge. No caller boolean alone proves hardware installation.
 * Initially all sibling EINT16..31 and GIC64 must be masked; active/foreign
 * owners are never taken. Child/parent register masked; enable only schedules
 * work. Worker drains then rearm+fresh F1 check to catch masked-edge races.
 * ISR masks own child+parent then signals only, never SDIO/GPIO IOLock/IAR/EOI.
 * Unknown sibling source is left pending and parent quarantined. Close first
 * disables/clears worker hook under its gate, unregisters parent/child, then
 * restores GPIO/puts notifier+native hold. Failed cleanup retains context.
 * Owner serializes public API lifetime. Does not change rails/FW/flash and
 * has no automatic hardware registration. Offline models are not real IRQ proof. */
#ifdef __cplusplus
}
#endif
#endif
