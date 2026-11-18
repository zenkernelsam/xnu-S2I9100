/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_AUDIO_GATED_H
#define I9100_AUDIO_GATED_H
#include "i9100_audio_native.h"
#include "i9100_pl330_ring_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_audio_gated;
int i9100_audio_gated_open(int enabled,struct i9100_peripheral_native *,struct i9100_audss *,struct i9100_mc1n2 *,
 struct i9100_gpio_lease *board_pins,uint32_t rate,uint32_t frames,uint32_t measured_bclk,struct i9100_audio_gated **);
int i9100_audio_gated_start(struct i9100_audio_gated *);
int i9100_audio_gated_queue(struct i9100_audio_gated *,const int16_t *,uint32_t frames);
int i9100_audio_gated_service(struct i9100_audio_gated *,uint32_t *completed_periods);
int i9100_audio_gated_release(struct i9100_audio_gated *); /* worker calls after feed, one fenced period only */
int i9100_audio_gated_status(struct i9100_audio_gated *,struct i9100_pcm_status *);
int i9100_audio_gated_output_ready(struct i9100_audio_gated *,uint32_t sample_rate);
int i9100_audio_gated_consumers_idle(struct i9100_audio_gated *);
int i9100_audio_gated_close(struct i9100_audio_gated **);
/* Default OFF. Borrowed whole GPZ/AUDSS/codec board leases remain live.
 * Prepare AP slave before pad mux; actual ring provides initial zero periods.
 * Service only takes the hardware fence; feed then explicit release chooses
 * safe producer data or zeros. Qualification is read-only under codec I2C
 * ownership, and never recursively mutes. Close stops ring/request/DMA before
 * pads=input and old I2S mode restore. Failed cleanup retains all references.
 */
#ifdef __cplusplus
}
#endif
#endif
