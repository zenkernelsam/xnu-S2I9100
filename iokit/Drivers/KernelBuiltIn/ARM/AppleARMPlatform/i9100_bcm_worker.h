/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_WORKER_H
#define I9100_BCM_WORKER_H
#include "i9100_bcm_pump.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_bcm_worker;
struct i9100_bcm_notify {
 struct i9100_bcm_worker *worker;uintptr_t self;uint32_t owned,busy;
};
struct i9100_bcm_output {
 void *cookie;int (*ethernet)(void *,const uint8_t *,unsigned,unsigned);
};
struct i9100_bcm_worker_status {
 uint32_t running,fault,stopping,ready,timer_wakes,notifier_wakes,notifiers,pending,worker_hold,control_state;
 uint32_t rx_frames,data_frames,dropped_frames,empty_reads,budget_yields,interrupt_status,mailbox;
 int32_t error,cleanup_error;
};
int i9100_bcm_worker_create(int enabled,struct i9100_bcm_boot *,struct i9100_native_transport_hold *session,
 uint32_t poll_us,const struct i9100_bcm_output *,struct i9100_bcm_worker **);
int i9100_bcm_worker_start(struct i9100_bcm_worker *);
int i9100_bcm_worker_join(struct i9100_bcm_worker *,const struct i9100_wlan_config *);
int i9100_bcm_worker_send(struct i9100_bcm_worker *,const uint8_t *,unsigned bytes,unsigned priority);
int i9100_bcm_worker_disconnect(struct i9100_bcm_worker *);
int i9100_bcm_worker_abort(struct i9100_bcm_worker *);
int i9100_bcm_worker_status(struct i9100_bcm_worker *,struct i9100_bcm_worker_status *);
int i9100_bcm_worker_destroy(struct i9100_bcm_worker **);
int i9100_bcm_worker_notifier_get(struct i9100_bcm_worker *,struct i9100_bcm_notify *);
int i9100_bcm_worker_signal(struct i9100_bcm_notify *); /* short IRQ-safe deferred signal, no SDIO */
int i9100_bcm_worker_notifier_put(struct i9100_bcm_notify *);
/* Default OFF. Real OSObject owner/IOWorkLoop/event/timer and native lease
 * APIs, not a raw heap pointer cast as OSObject. Pump and controller are
 * private to this serial gate; one native session hold lasts until FW halt,
 * another worker hold protects callbacks through source removal.
 * The session's exact lease must be the F2 IO cookie. Timer is bounded500us
 * ..10ms; each tick reads at most4 frames. Repeated lock contention500ms
 * stops/quarantines instead of spinning; errors never replay/reset firmware.
 * Notifier tokens retain the worker; signal only marks+wakes. The board IRQ
 * provider still masks/acks/rearms GPX2.5/EINT21/GIC64 and unregisters/syncs
 * before notifier_put. This module does NOT install that physical IRQ hook.
 * Destroy refuses live notifiers, JOINING/UP/DISCONNECTING state and callback
 * thread. Disconnect+mask-restore or explicit abort first; then disable and
 * remove event sources, close pump/control, put worker hold, release object.
 * Failed cleanup leaves a nonnull owner and diagnostic status; the session
 * hold remains until explicit boot/core/native shutdown. No IO80211 service,
 * network configuration, scan or RF authorization is provided by creation. */
#ifdef __cplusplus
}
#endif
#endif
