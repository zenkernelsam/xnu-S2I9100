/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PL330_RING_H
#define I9100_PL330_RING_H
#include "i9100_pl330.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_ring_error {I9100_RING_ARGUMENT=-1860,I9100_RING_DISABLED=-1861,
 I9100_RING_OWNER=-1862,I9100_RING_BUSY=-1863,I9100_RING_IO=-1864,
 I9100_RING_TIMEOUT=-1865,I9100_RING_FAULT=-1866,I9100_RING_OVERFLOW=-1867};
enum i9100_ring_phase {I9100_RING_IDLE=1,I9100_RING_PREPARED=2,I9100_RING_RUNNING=3,
 I9100_RING_FENCED=4,I9100_RING_STOPPING=5,I9100_RING_DRAINING=6};
struct i9100_pl330_ring_io {
 struct i9100_pl330_io registers;
 void *requests_cookie;
 int (*quiesce_requests)(void *); /* zero only after actual I2S requests are disabled/read back */
};
struct i9100_pl330_ring;
struct i9100_pl330_ring_buffer_cookie {
 struct i9100_pl330_ring *ring;uintptr_t self;uint32_t slot;
};
struct i9100_pl330_ring_status {
 uint32_t enabled,active,fault,phase,held_mask,pending_slot,pending,expected_slot;
 uint32_t program_bytes,period_frames,sample_rate,cs,cpc,events,interrupts,manager_fault,channel_fault;
 uint64_t completed_periods,completed_frames,completed_silence_periods,start_ns,fence_ns;
 int32_t error,cleanup_error;
};
struct i9100_pl330_ring {
 struct i9100_pl330_ring_io io;
 struct i9100_dma_buffer *buffers[4]; /* period0, period1, loop code, immutable event-drain code */
 uintptr_t cpu[4],self;uint32_t pa[4],bytes[4];
 uint32_t initialized,enabled,owned,active,fault,phase,held_mask;
 uint32_t gate_offset[2],program_bytes,period_frames,sample_rate,timeout_ms;
 uint32_t expected_slot,pending_slot,pending,queued,peak[2],saved_inten;
 uint64_t completed_periods,completed_frames,completed_silence_periods,start_ns,fence_ns,last_release_ns;
 int32_t error,cleanup_error;
};
int i9100_pl330_ring_init(struct i9100_pl330_ring *,const struct i9100_pl330_ring_io *);
int i9100_pl330_ring_buffer_cookie_init(struct i9100_pl330_ring_buffer_cookie *,struct i9100_pl330_ring *,unsigned slot);
int i9100_pl330_ring_buffer_idle(void *); /* actual per-buffer WFE fence or stopped controller; no lock recursion */
int i9100_pl330_ring_encode(uint32_t period0,uint32_t period1,uint32_t frames,
 uint8_t *code,uint32_t capacity,uint32_t *used,uint32_t gate_offsets[2]);
int i9100_pl330_ring_enable(struct i9100_pl330_ring *,int enabled);
int i9100_pl330_ring_prepare(struct i9100_pl330_ring *,struct i9100_dma_buffer *period0,
 struct i9100_dma_buffer *period1,struct i9100_dma_buffer *code,struct i9100_dma_buffer *drain,
 uint32_t sample_rate,uint32_t period_frames);
int i9100_pl330_ring_start(struct i9100_pl330_ring *);
int i9100_pl330_ring_poll(struct i9100_pl330_ring *,uint32_t *new_completion);
int i9100_pl330_ring_queue(struct i9100_pl330_ring *,const int16_t *samples,uint32_t frames);
int i9100_pl330_ring_release_period(struct i9100_pl330_ring *);
int i9100_pl330_ring_stop(struct i9100_pl330_ring *);
int i9100_pl330_ring_status(struct i9100_pl330_ring *,struct i9100_pl330_ring_status *);
/* Default OFF. Exclusively uses PDMA0 channel0, IRQ event3, internal event4
 * and fixed I2S0 TX request12/FIFO. No arbitrary programs/MMIO/user addresses.
 * The hardware loop never repeats a period without one owner-issued release.
 * WMB -> SEV3 -> WFE4 yields exactly one unacknowledged completion, avoiding
 * IRQ coalescing counts. CPC/CS/event/owner validation precedes CPU takeover.
 * Each period is returned and re-prepared through the actual DMA guard,
 * always pinned; the other period and code remain DEVICE-owned. Only the
 * currently fenced period may be changed, and its samples are <=-30dB.
 * Missing producer data is explicitly replaced by zeros on release.
 *
 * The four distinct allocation cookies use ring_buffer_idle, and stay live
 * until successful stop. Their owner serializes allocation/API/destroy and
 * never exposes the underlying buffer API or calls this in an IRQ. DMA
 * callbacks read this state while its outer engine mutex is already held.
 * Stop quiesces actual consumer requests before KILL, drains any internal
 * event through separate immutable WFE+END code, then reclaims/unpins.
 * Failed quiesce, KILL, event drain, sync or unpin retains resources and the
 * first cause; nothing automatically restarts/replays or frees active memory.
 * This is transport continuity, not proof of real FIFO timing/acoustic
 * position/underrun behavior. The PCM/codec owner must still establish mute,
 * real clocks/pads, FIFO status and physical gain/pop/stop acceptance.
 */
#ifdef __cplusplus
}
#endif
#endif
