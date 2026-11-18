/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_AUDIO_WORKER_H
#define I9100_AUDIO_WORKER_H
#include "i9100_audio_board_native.h"
#include "i9100_irq_registry.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_audio_worker;
struct i9100_audio_worker_status {
 uint64_t completed_periods;
 uint32_t running,fault,irq_wakes,timer_wakes,pending,board_held,session_owned;
 int32_t error;
 struct i9100_pcm_status last_pcm;uint32_t last_pcm_valid;
 uint32_t staged_frames,staging_capacity;uint64_t enqueued_frames,staged_consumed;
 uint32_t output_requested,output_prepared,output_active,closed,detaching;
 int32_t cleanup_error,irq_error;
 struct i9100_audio_board_status last_board;uint32_t last_board_valid;
};
int i9100_audio_worker_create(int enabled,struct i9100_audio_board_native *,struct i9100_irq_registry *,
 int use_irq,int verified_dispatch_hook,uint32_t poll_us,struct i9100_audio_worker **);
int i9100_audio_worker_prepare(struct i9100_audio_worker *,uint32_t independently_measured_bclk);
int i9100_audio_worker_queue(struct i9100_audio_worker *,const int16_t *,uint32_t frames);
int i9100_audio_worker_enqueue(struct i9100_audio_worker *,const int16_t *kernel_samples,uint32_t frames);
int i9100_audio_worker_flush(struct i9100_audio_worker *); /* queue final short period, zero-pad its tail */
/* enqueue success means the whole block was accepted into CPU staging.
 * A later queue/driver fault is retained in status and never causes replay. */
int i9100_audio_worker_start(struct i9100_audio_worker *);
int i9100_audio_worker_output_prepare(int enabled,struct i9100_audio_worker *,enum i9100_mc1n2_output,int32_t requested_mdb);
int i9100_audio_worker_output_activate(struct i9100_audio_worker *);
int i9100_audio_worker_output_mute(struct i9100_audio_worker *);
int i9100_audio_worker_stop(struct i9100_audio_worker *);
int i9100_audio_worker_status(struct i9100_audio_worker *,struct i9100_audio_worker_status *);
int i9100_audio_worker_destroy(struct i9100_audio_worker **);
/* Default OFF. Real IOWorkLoop serializes timer, deferred IRQ and control
 * operations. IRQ67 only masks its source, marks pending and signals work;
 * no FIFO, I2C, stop loop, extra IAR read or EOI in the handler.
 * Register masked, open PCM, then start/unmask. IRQ mode requires an actual
 * caller-installed IAR->registry dispatch hook; this module does not claim
 * to install it or modify the live MCT dispatcher. Polling needs no hook.
 * A timer backs up a SEV arriving before DMAEND; finite periods may still
 * underrun, and this is not proof of seamless playback or acoustic position.
 * Board hold prevents premature close. Stop disables/cancels sources before
 * PCM close. Destroy unregisters IRQ and synchronously removes event sources
 * before releasing the exact board worker lease; every failure retains the
 * context. One worker excludes legacy board mutations and other workers.
 *
 * Optional output prepare/activate/mute all run on this same workloop.
 * Prepare requires empty PCM hardware queues; CPU staging may accumulate
 * while the output is muted. Start then transmits actual zero pre-roll and
 * activate checks native PCM readiness before any low-gain write. No automatic
 * activation or retry: a not-ready refusal keeps silence; a post-IO fault
 * halts the session. Every ring/timer/IRQ/driver fault attempts silence and
 * DMA/output shutdown, preserving first cause and separate cleanup error.
 * IRQ errors defer all shutdown/I2C work to event/timer processing.
 * Owner serializes pointer lifetime/destroy, and never destroys on work thread.
 */
#ifdef __cplusplus
}
#endif
#endif
