/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_CLOCK_NATIVE_H
#define I9100_EMMC_CLOCK_NATIVE_H
#include "i9100_emmc_clock.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_emmc_clock_observer;
int i9100_emmc_clock_observe_open(int enabled,uint32_t xxti_hz,uint32_t xusbxti_hz,struct i9100_emmc_clock_observer **);
int i9100_emmc_clock_observe_copy(struct i9100_emmc_clock_observer *,uint8_t out[I9100_EMMC_CLOCK_BYTES]);
int i9100_emmc_clock_observe_close(struct i9100_emmc_clock_observer **);
void ios6i9100_register_emmc_clock_observation(void);
extern uint8_t i9100_emmc_clock_observation[512];
extern volatile uint32_t i9100_emmc_clock_observation_diag[8];
/* Explicit read-only pre-probe observer. No card initialization or write
 * callbacks. Uses the existing host lease,5 private maps and actual clock
 * capture; error retains raw record and live owner when host remains busy.
 * The optional PE hook only runs i9100emmcobserve=1 and explicit24MHz roots,
 * rejecting simultaneous native root/legacyRO flags. Default maps nothing.
 * Snapshot can be read via the existing authorized DRAM read-memory tool;
 * no new USB command or arbitraryMMIO interface is introduced. */
#ifdef __cplusplus
}
#endif
#endif
