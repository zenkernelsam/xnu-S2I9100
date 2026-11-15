/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PCM_STREAM_H
#define I9100_PCM_STREAM_H
#include "i9100_pl330.h"
#include "i9100_i2s.h"
#ifdef __cplusplus
extern "C" {
#endif

enum i9100_pcm_error {I9100_PCM_ARGUMENT=-1160,I9100_PCM_DISABLED=-1161,
 I9100_PCM_BUSY=-1162,I9100_PCM_IO=-1163,I9100_PCM_OWNER=-1164,
 I9100_PCM_FAULT=-1165,I9100_PCM_OVERFLOW=-1166};
struct i9100_pcm_io {
 void *cookie;
 int (*lock)(void *);void (*unlock)(void *);
 int (*ensure_muted)(void *); /* zero only after DAC/outputs/amps are verified silent */
};
struct i9100_pcm_status {
 uint64_t queued_frames,completed_frames,completed_periods,zero_periods;
 uint32_t enabled,prepared,started,fault,held_mask,ready_mask,in_flight;
 uint32_t sample_rate,period_frames;
 int32_t last_error,cleanup_error;
};
struct i9100_pcm_stream {
 struct i9100_pcm_io io;
 struct i9100_pl330 *dma;struct i9100_i2s *i2s;
 struct i9100_dma_buffer *period[2],*code;
 uintptr_t self;
 uint64_t queued_frames,completed_frames,completed_periods,zero_periods;
 uint32_t initialized,enabled,prepared,started,fault,i2s_owned,held_mask;
 uint32_t ready_mask,in_flight,next_index,sample_rate,period_frames,period_bytes,timeout_ms;
 int32_t last_error,cleanup_error;
};
int i9100_pcm_stream_init(struct i9100_pcm_stream *,const struct i9100_pcm_io *,
 struct i9100_pl330 *,struct i9100_i2s *,struct i9100_dma_buffer *period0,
 struct i9100_dma_buffer *period1,struct i9100_dma_buffer *code);
int i9100_pcm_stream_enable(struct i9100_pcm_stream *,int enabled);
int i9100_pcm_stream_prepare(struct i9100_pcm_stream *,uint32_t sample_rate,
 uint32_t period_frames,uint32_t measured_bclk,int exclusive_gpio_clock_verified);
int i9100_pcm_stream_queue(struct i9100_pcm_stream *,const int16_t *kernel_samples,uint32_t frames);
int i9100_pcm_stream_start(struct i9100_pcm_stream *);
int i9100_pcm_stream_service(struct i9100_pcm_stream *,uint32_t *completed_periods);
int i9100_pcm_stream_stop(struct i9100_pcm_stream *);
int i9100_pcm_stream_status(struct i9100_pcm_stream *,struct i9100_pcm_status *);
/* Default OFF. One exclusive PL330/I2S owner, two distinct DMA allocations
 * and a private finite-program allocation. Prepare pins all three until a
 * successful stop. Callbacks and buffer/engine structs remain live meanwhile;
 * no direct lower-level API access or copying this struct while it is owned.
 * Lock order: worker stream -> I2S or PL330 -> individual allocation. I2S's
 * stop callback also uses this PL330; these mutexes must all be distinct.
 * No IRQ-side calls, user addresses, mmap or allocation in this core.
 *
 * Queue copies at most one period of caller-owned kernel PCM (stereo int16),
 * always <=-30dB, padding with actual zeros. Missing queued periods are zero
 * filled and counted; queued silence is not counted as producer starvation.
 * Service processes at most one completed finite job and arms the next; no
 * indefinite hardware loop or seamless timing guarantee. A real workloop,
 * FIFO underrun detection, codec clocks/output sequencing and HAL remain.
 * Position counts confirmed DMA words copied to the I2S FIFO, not acoustic
 * playback position. A stuck/faulted stop retains pins and never auto-retries.
 * The provider keeps physical outputs muted throughout, with no unmute API.
 */
#ifdef __cplusplus
}
#endif
#endif
