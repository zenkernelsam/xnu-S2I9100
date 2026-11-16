/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_RADIO_SURVEY_H
#define I9100_RADIO_SURVEY_H
#include "i9100_power_shared.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_radio_survey {
 uint8_t pmic_id,pmic_version,control1,ldo17;
 uint32_t ap_clock_programmed,cp_clock_programmed,vtf_mode,vtf_programmed_mv;
 uint32_t wiring_verified,electrical_verified;
};
int i9100_radio_survey_read(struct i9100_power_read_client *,struct i9100_radio_survey *);
/* Read-only precursor to a supply provider. PMIC fields are actual individual
 * reads; report is published only after all four succeed, with double-read
 * of controls to reject a changing configuration. No CLOCK/LDO write/disable.
 * LDO17 is shared with external SD in the i9100 DTS. Nominal programmed mV
 * and EN32KHZ bits are NOT electrical proof; these report fields stay0 until
 * a separate board/measurement owner verifies the routing and real signals.
 * This function deliberately does not implement radio_supply.verify or
 * allow power-on based only on a fixed32768/nominal voltage assumption. */
#ifdef __cplusplus
}
#endif
#endif
