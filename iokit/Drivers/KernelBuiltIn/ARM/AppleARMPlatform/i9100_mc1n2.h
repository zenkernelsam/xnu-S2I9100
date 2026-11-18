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
 I9100_MC_IO=-643,I9100_MC_ID=-644,I9100_MC_TIMEOUT=-645,I9100_MC_NOT_READY=-646,I9100_MC_OWNER=-647};
struct i9100_mc1n2;
struct i9100_mc1n2_output_guard {
 void *cookie;
 int (*lease_valid)(void *); /* live exclusive rails/MCLK/pads/board owner */
 int (*consumers_idle)(void *); /* real I2S/PDMA idle; needed for power changes */
 int (*stream_ready)(void *,uint32_t sample_rate); /* real PCM qualification, never a constant */
};
struct i9100_mc1n2_output_lease {
 struct i9100_mc1n2 *codec;
 uintptr_t self;
 uint32_t generation,owned;
};
struct i9100_mc1n2_clock_guard {
 void *cookie;
 int (*lease_valid)(void *); /* read-only: zero=rails/MCLK/pad direction/exclusive board owner verified */
 int (*consumers_idle)(void *); /* read-only: zero=real I2S/DMA consumers are quiescent */
};
struct i9100_mc1n2 {
 struct i9100_peripheral_i2c io;
 uintptr_t self;
 uint32_t initialized,enabled,fault,muted,gain_armed;
 uint32_t cold_stage,cold_owned,sample_rate,mclk_hz,failure_stage;
 struct i9100_mc1n2_clock_guard clock_guard;
 uint32_t clock_owned,clock_active,clock_stage,clock_failure_stage;
 uint8_t clock_saved_pad,clock_saved_start,clock_saved_fs;
 int32_t clock_error,clock_cleanup_error;
 int32_t cold_error;
 int32_t actual_gain_mdb;
 uint8_t id,analog_gain;
 struct i9100_mc1n2_output_guard output_guard;
 struct i9100_mc1n2_output_lease *output_owner;
 uint32_t output_generation,output_kind,output_stage,output_powered,output_running,output_failure_stage;
 int32_t output_error,output_cleanup_error;
 uint8_t output_saved_power[3],output_saved_routes[7],output_saved_source,output_saved_swap,output_saved_mode;
 uint8_t output_gain_code;
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
int i9100_mc1n2_output_prepare(int enabled,struct i9100_mc1n2 *,
 enum i9100_mc1n2_output,int32_t requested_mdb,const struct i9100_mc1n2_output_guard *,
 struct i9100_mc1n2_output_lease *);
int i9100_mc1n2_output_activate(struct i9100_mc1n2_output_lease *);
int i9100_mc1n2_output_stop(struct i9100_mc1n2_output_lease *);
/* Default OFF, exclusive AA/AB output diagnostic. Prepare requires cold5,
 * owned clock4 and idle AP/DMA; it executes the vendor DAC2->3->1, analog DA,
 * CP/HP or two-stage SP sequencing with all gains at zero. AA DA=08, AB=18.
 * Manual mode uses >=300us CP, >=150us SP stage separation and >=9/13ms HP/SP
 * settling; no APM-only output-ready inference. All relevant writes read back.
 * Activate additionally requires a live PCM provider: confirmed zero period,
 * actual I2S/DMA status and bounded NC samples. Digital DAC code66=-30dB and
 * HP/SP analog code1=-36dB are the only audible settings. No arbitrary gains.
 * Public mute silences an owned output without losing its CP/DAC/amp lease.
 * Stop silences first, then requires idle consumers before DA/DAC/CP shutdown,
 * waits for DAC flags/CPPDRDY and restores only the saved safe standby route.
 * Any unconfirmed cleanup retains the exact lease and clock/cold owners.
 * Owner serializes codec/PCM controls and lifetime; guards do not recurse into
 * codec/I2C, and all providers remain live through successful output_stop.
 * Registers/PCM qualification do not prove board rails, clocks or acoustics.
 */
/* Clock-only AA/AB24MHz/44.1/48kHz: manual32fs master, DIR0/SRC/timer START=4c,
 * BCLK/LRCK output pad DDR=50, PCM data pads remain high-Z. DAC and every amp
 * stay muted/off. Board guard must prove no competing AP clock output and
 * actual idle consumers before start/stop; it performs no bus recursion.
 * The guard and borrowed rails/MCLK/board owners remain live until successful
 * clock_stop/cold_stop. Errors retain clock_owned until verified shutdown.
 * clock_active means programmed/readback-verified, not measured frequency;
 * independent BCLK32fs measurement is still required by the native adapter.
 * Clock-only entry enables no output; output_prepare is a separate opt-in. */
/* Cold initialization supports AA(78)/AB(79),24MHz CMOS,44.1/48kHz only.
 * Programs actual PLL/DIO/LDOA/VREF, verifies ready and safe amplifier/mute
 * registers; leaves DAC,clock pads and all output amplifiers disabled.
 * AA uses its BASE PLL/DP0 sequence and ANA power unlock; AB uses CODEC PLL.
 * Both retain separate, verified shutdown ordering. Output/continuous PCM/HAL
 * remain separate required stages. Cold state5 means configured+muted. */
/* MC1N2AA/AB only, slave0x3a. Real wire register=logical address<<1,
 * bank address/window transaction serialized on the bus. No arbitrary write
 * API. Legacy enable/stage paths silence and power down the amplifiers.
 * Owned-output mute instead retains powered resources at verified zero gain.
 * Safe HP/SP code1=-36dB; code2=-28dB is never staged. Legacy gain staging is
 * not unmute/analog initialization and is refused while an output is owned.
 * Clock/cold/disable cannot bypass the output owner's explicit shutdown. */
#ifdef __cplusplus
}
#endif
#endif
