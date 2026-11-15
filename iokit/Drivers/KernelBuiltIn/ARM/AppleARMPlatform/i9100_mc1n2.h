/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MC1N2_H
#define I9100_MC1N2_H
#include "i9100_peripheral_io.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_mc1n2_bank {I9100_MC_DIRECT=0,I9100_MC_BASE=1,I9100_MC_ANALOG=2,I9100_MC_CODEC=3,I9100_MC_MIXER=4};
enum i9100_mc1n2_output {I9100_MC_HEADPHONES=1,I9100_MC_SPEAKER=2};
enum i9100_mc1n2_error {I9100_MC_ARGUMENT=-640,I9100_MC_DISABLED=-641,I9100_MC_BUSY=-642,
 I9100_MC_IO=-643,I9100_MC_ID=-644,I9100_MC_TIMEOUT=-645,I9100_MC_NOT_READY=-646};
struct i9100_mc1n2_clock_guard {
 void *cookie;
 int (*lease_valid)(void *); /* read-only: zero=rails/MCLK/pad direction/exclusive board owner verified */
 int (*consumers_idle)(void *); /* read-only: zero=real I2S/DMA consumers are quiescent */
};
struct i9100_mc1n2 {
 struct i9100_peripheral_i2c io;
 uint32_t initialized,enabled,fault,muted,gain_armed;
 uint32_t cold_stage,cold_owned,sample_rate,mclk_hz,failure_stage;
 struct i9100_mc1n2_clock_guard clock_guard;
 uint32_t clock_owned,clock_active,clock_stage,clock_failure_stage;
 uint8_t clock_saved_pad,clock_saved_start,clock_saved_fs;
 int32_t clock_error,clock_cleanup_error;
 int32_t cold_error;
 int32_t actual_gain_mdb;
 uint8_t id,analog_gain;
};
int i9100_mc1n2_init(struct i9100_mc1n2 *,const struct i9100_peripheral_i2c *);
int i9100_mc1n2_enable(struct i9100_mc1n2 *,int enabled);
int i9100_mc1n2_mute(struct i9100_mc1n2 *);
int i9100_mc1n2_safe_gain(int32_t requested_mdb,uint8_t *hardware_code,int32_t *actual_mdb);
int i9100_mc1n2_stage_gain(struct i9100_mc1n2 *,enum i9100_mc1n2_output,int32_t requested_mdb);
int i9100_mc1n2_read(struct i9100_mc1n2 *,enum i9100_mc1n2_bank,uint8_t reg,uint8_t *value);
int i9100_mc1n2_cold_init(struct i9100_mc1n2 *,uint32_t verified_mclk_hz,uint32_t sample_rate,int exclusive_board_power_clock_verified);
int i9100_mc1n2_cold_stop(struct i9100_mc1n2 *);
int i9100_mc1n2_clock_start(struct i9100_mc1n2 *,const struct i9100_mc1n2_clock_guard *);
int i9100_mc1n2_clock_stop(struct i9100_mc1n2 *);
/* Clock-only AB24MHz/44.1/48kHz: manual32fs master, DIR0/SRC/timer START=4c,
 * BCLK/LRCK output pad DDR=50, PCM data pads remain high-Z. DAC and every amp
 * stay muted/off. Board guard must prove no competing AP clock output and
 * actual idle consumers before start/stop; it performs no bus recursion.
 * The guard and borrowed rails/MCLK/board owners remain live until successful
 * clock_stop/cold_stop. Errors retain clock_owned until verified shutdown.
 * clock_active means programmed/readback-verified, not measured frequency;
 * independent BCLK32fs measurement is still required by the native adapter.
 * No PCM power, output route, amplifier or audible playback is enabled here. */
/* Cold initialization is currently AB(79),24MHz CMOS,44.1/48kHz only.
 * Programs actual PLL/DIO/LDOA/VREF, verifies ready and safe amplifier/mute
 * registers; leaves DAC,clock pads and all output amplifiers disabled.
 * AA has a different vendor implementation and is explicitly unsupported
 * for this stage, rather than fed AB settings. Output/continuous PCM/HAL
 * remain separate required stages. Cold state5 means configured+muted. */
/* MC1N2AA/AB only, slave0x3a. Real wire register=logical address<<1,
 * bank address/window transaction serialized on the bus. No arbitrary write
 * API. All enable/stage/error paths silence DAC and HP/SP and power down the
 * output amplifiers. Safe HP/SP code1=-36dB; code2=-28dB is never staged.
 * Gain staging is not unmute/analog initialization: cold PLL/DIO/reference
 * and output-power/pop sequencing still require their own reviewed adapter.
 * This core intentionally cannot energize speaker/headphones. */
#ifdef __cplusplus
}
#endif
#endif
