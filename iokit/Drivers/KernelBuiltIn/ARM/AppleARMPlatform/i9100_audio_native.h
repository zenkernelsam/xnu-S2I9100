/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_AUDIO_NATIVE_H
#define I9100_AUDIO_NATIVE_H
#include "i9100_peripheral_native.h"
#include "i9100_audss.h"
#include "i9100_mc1n2.h"
#include "i9100_pcm_stream.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_audio_native;
int i9100_audio_native_open(int enabled,struct i9100_peripheral_native *,
 struct i9100_audss *,struct i9100_mc1n2 *,uint32_t rate,uint32_t period_frames,
 uint32_t measured_bclk,int exclusive_board_clocks_verified,struct i9100_audio_native **);
int i9100_audio_native_queue(struct i9100_audio_native *,const int16_t *kernel_samples,uint32_t frames);
int i9100_audio_native_start(struct i9100_audio_native *);
int i9100_audio_native_service(struct i9100_audio_native *,uint32_t *completed_periods);
int i9100_audio_native_status(struct i9100_audio_native *,struct i9100_pcm_status *);
int i9100_audio_native_close(struct i9100_audio_native **);
/* Default OFF actual IOKit private PDMA0/I2S0/GIC read-only maps and DMA32 NC
 * allocations. Requires one whole PDMA0/GPZ owner; no global reset or IRQ hook.
 * Uses PDMA0 channel0/event3/request12 and GPZ0..6/AF2, as pinned Exynos4210.
 * UART/radio/PMU/EPLL/codec rails are not touched. AUDSS and AB79/24MHz codec
 * cold/owned clock-only states are borrowed from live providers. Clock pads/
 * DIR0-SRC-timer(4c) must be configured and BCLK measured32fs; cold stage5
 * alone is insufficient. This adapter never enables codec amps/clock pads.
 * AP slave mode is prepared before GPIO mux, and pads become input before
 * restoring the old I2S mode, so two clock outputs cannot be reconnected.
 *
 * Parent, AUDSS, codec/I2C/power/MCLK providers remain owned and unchanged
 * until close succeeds. An open error with a nonnull result is retained
 * cleanup ownership, not a ready device. After any close attempt only status
 * or explicit close is allowed; never destroy borrowed providers on failure.
 * Owner serializes pointer lifetime/close with API calls. Worker calls only;
 * no automatic thread/IRQ scheduling or user clients. No raw stream pointers.
 */
#ifdef __cplusplus
}
#endif
#endif
