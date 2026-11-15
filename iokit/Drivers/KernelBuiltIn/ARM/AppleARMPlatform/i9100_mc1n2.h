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
struct i9100_mc1n2 {
 struct i9100_peripheral_i2c io;
 uint32_t initialized,enabled,fault,muted,gain_armed;
 int32_t actual_gain_mdb;
 uint8_t id,analog_gain;
};
int i9100_mc1n2_init(struct i9100_mc1n2 *,const struct i9100_peripheral_i2c *);
int i9100_mc1n2_enable(struct i9100_mc1n2 *,int enabled);
int i9100_mc1n2_mute(struct i9100_mc1n2 *);
int i9100_mc1n2_safe_gain(int32_t requested_mdb,uint8_t *hardware_code,int32_t *actual_mdb);
int i9100_mc1n2_stage_gain(struct i9100_mc1n2 *,enum i9100_mc1n2_output,int32_t requested_mdb);
int i9100_mc1n2_read(struct i9100_mc1n2 *,enum i9100_mc1n2_bank,uint8_t reg,uint8_t *value);
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
