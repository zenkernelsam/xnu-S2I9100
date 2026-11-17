/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_VIBRATOR_H
#define I9100_VIBRATOR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_vibrator_error {I9100_VIB_ARGUMENT=-520,I9100_VIB_DISABLED=-521,
 I9100_VIB_BUSY=-522,I9100_VIB_IO=-523,I9100_VIB_TIMEOUT=-524};
struct i9100_vibrator_io {
 void *cookie;
 int (*lock)(void *);void (*unlock)(void *);
 int (*haptic_read)(void *,uint8_t,uint8_t *);
 int (*haptic_write)(void *,uint8_t,uint8_t);
 int (*rail)(void *,int); /* shared PMIC owner; EUR LDO14 vmotor, not charger bus takeover */
 int (*pwm_config)(void *,uint32_t period_ns,uint32_t duty_ns);
 int (*pwm_enable)(void *,int);
 int (*arm_timeout)(void *,uint32_t milliseconds);
 void (*cancel_timeout)(void *);
 uint64_t (*now_ns)(void *);
};
struct i9100_vibrator {
 struct i9100_vibrator_io io;
 uint64_t deadline,started;
 uint32_t initialized,enabled,active,fault,rail_owned,period_ns,duty_ns;
 uint8_t saved_config;
};
int i9100_vibrator_init(struct i9100_vibrator *,const struct i9100_vibrator_io *,uint32_t verified_period_ns);
int i9100_vibrator_enable(struct i9100_vibrator *,int);
int i9100_vibrator_pulse(struct i9100_vibrator *,uint32_t milliseconds,unsigned amplitude_permille);
int i9100_vibrator_stop(struct i9100_vibrator *);
int i9100_vibrator_timeout(struct i9100_vibrator *); /* workloop/thread, never I2C in IRQ */
/* MAX8997 haptic slave0x48/config2, LRA + external PWM /256. EUR source
 * period38022ns; regional periods are not silently interchangeable.
 * Early diagnostic limit250ms and ±5% about the zero-drive50% duty centre.
 * No extension/retrigger while active; failures always attempt PWM/PMIC/rail
 * shutdown and keep fault state if any part cannot be proved off.
 * Preparation time is part of the fixed deadline. An early timer callback
 * rearms the remaining budget; arm failure immediately attempts shutdown.
 * PMIC and PWM providers must have been separately identified/owned. */
#ifdef __cplusplus
}
#endif
#endif
