/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PL330_RING_NATIVE_H
#define I9100_PL330_RING_NATIVE_H
#include "i9100_pl330_ring.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_pl330_ring_native;
struct i9100_ring_consumer {
 void *cookie;
 int (*verify_resources)(void *); /* actual owned powered AUDSS/GPZ/codec clock/BCLK profile */
 int (*mute)(void *); /* verify actual codec/amp mute; never unmute here */
 int (*start_requests)(void *);
 int (*quiesce_requests)(void *);
};
int i9100_pl330_ring_native_open(int enabled,struct i9100_peripheral_native *,const struct i9100_ring_consumer *,
 uint32_t sample_rate,uint32_t period_frames,struct i9100_pl330_ring_native **);
int i9100_pl330_ring_native_start(struct i9100_pl330_ring_native *);
int i9100_pl330_ring_native_poll(struct i9100_pl330_ring_native *,uint32_t *new_completion);
int i9100_pl330_ring_native_queue(struct i9100_pl330_ring_native *,const int16_t *,uint32_t frames);
int i9100_pl330_ring_native_release_period(struct i9100_pl330_ring_native *);
int i9100_pl330_ring_native_status(struct i9100_pl330_ring_native *,struct i9100_pl330_ring_status *);
int i9100_pl330_ring_native_close(struct i9100_pl330_ring_native **);
int i9100_pl330_ring_native_output_ready(struct i9100_pl330_ring_native *,uint32_t sample_rate);
int i9100_pl330_ring_native_consumers_idle(struct i9100_pl330_ring_native *);
/* Default OFF actual PDMA0/I2S0/GIC maps and four real wired DMA32 NC
 * allocations. One parent hold + PDMA clock lease; GPIO/AUDSS/codec leases
 * remain with the verified consumer until successful close. Read-back
 * validates I2S requests/format and GIC masking independently of callbacks.
 * No codec output, pin mux, parent PLL/domain or IRQ registration is enabled.
 * Poll/queue/release are worker-side only. Every failure attempts mute and
 * request shutdown/stop, with separate first/cleanup errors and no resume.
 * Failed close retains maps, clocks, allocations and parent; status works
 * from a saved snapshot after partial cleanup. Owner serializes pointer
 * lifetime and never destroys the consumer while this context is retained.
 * The existing board/workloop/HAL still use finite PCM until an explicit
 * opt-in integration selects this transport; no live boot hook is changed.
 */
#ifdef __cplusplus
}
#endif
#endif
