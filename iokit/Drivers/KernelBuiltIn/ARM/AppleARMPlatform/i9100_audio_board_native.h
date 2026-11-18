/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_AUDIO_BOARD_NATIVE_H
#define I9100_AUDIO_BOARD_NATIVE_H
#include "i9100_audio_native.h"
#include "i9100_audio_power.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_audio_board_native;
struct i9100_audio_board_profile {
 struct i9100_audss_rates verified_rates;
 struct i9100_audss_config audss_config;
 uint32_t verified_i2c6_pclk_hz,sample_rate,period_frames,use_config;
 int exclusive_u1_rails_clocks_verified;
};
struct i9100_audio_board_status {
 uint32_t phase,stage,mclk_owned,domain_owned,audss_active,pins_owned,bus_owned;
 uint32_t codec_enabled,cold_owned,clock_owned,pcm_owned;
 uint32_t worker_holds;
 int32_t error;
 uint32_t output_owned,output_powered,output_running,output_stage;
 int32_t output_error,output_cleanup_error,output_gain_mdb;
};
int i9100_audio_board_open(int enabled,struct i9100_peripheral_native *,const struct i9100_audio_board_profile *,struct i9100_audio_board_native **);
int i9100_audio_board_pcm_open(struct i9100_audio_board_native *,uint32_t independently_measured_bclk);
int i9100_audio_board_pcm_close(struct i9100_audio_board_native *);
int i9100_audio_board_output_prepare(int enabled,struct i9100_audio_board_native *,
 enum i9100_mc1n2_output,int32_t requested_mdb);
int i9100_audio_board_output_activate(struct i9100_audio_board_native *);
int i9100_audio_board_output_stop(struct i9100_audio_board_native *);
int i9100_audio_board_hold(struct i9100_audio_board_native *);
int i9100_audio_board_unhold(struct i9100_audio_board_native *);
int i9100_audio_board_queue(struct i9100_audio_board_native *,const int16_t *,uint32_t frames);
int i9100_audio_board_start(struct i9100_audio_board_native *);
int i9100_audio_board_service(struct i9100_audio_board_native *,uint32_t *completed_periods);
int i9100_audio_board_status(struct i9100_audio_board_native *,struct i9100_audio_board_status *,struct i9100_pcm_status *optional_pcm);
int i9100_audio_board_close(struct i9100_audio_board_native **);
/* Default OFF actual IOKit PMU/ChipID/AUDSS/I2S/PDMA0 maps. Exynos4210 mask
 * from plat/cpu.h; no 4x12 CMU C93C writes, PLL reprogramming, PMIC rail/gain
 * changes or automatic driver registration. Only a verified U1/24MHz board
 * owner may open; actual codec external rails and I2C6 PCLK are supplied.
 * Codec ID/mute readback precedes any MCLK-source change. If register access
 * with inherited MCLK cannot establish silence, open stops safely; board
 * reset/rail support is still required before attempting that hardware case.
 * Owns I2C6, GPIO GPZ, PMU leases, AUDSS, AA/AB cold/clock-only state and optional
 * PCM child. GPZ stays leased until codec clocks are off; PCM borrows it.
 * I2C6 uses an exact owned lease; legacy close/callbacks cannot steal it.
 * A parent hold protects managers/maps before and after hardware claims.
 * PCM opening requires separately measured BCLK; register-ready is not a
 * measurement. Normal open keeps outputs muted/off. Explicit output_prepare
 * powers one route at zero gain; activate checks the actual native PCM path,
 * confirmed silence and live DMA/I2S before digital-30dB/analog-36dB output.
 * A mute keeps the owned route available; PCM close mutes, stops DMA and then
 * closes the output lease before clock shutdown. Failures retain the board.
 * The optional worker schedules PCM/IRQ; output/HAL activation is separate.
 * A nonnull result on error retains failed cleanup ownership; only explicit
 * close may recover it. Parent, profile evidence and maps remain live until
 * close succeeds; owner serializes pointer lifetime with API calls.
 */
#ifdef __cplusplus
}
#endif
#endif
