/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PWM_HAPTIC_H
#define I9100_PWM_HAPTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_pwm_error {I9100_PWM_ARGUMENT=-1000,I9100_PWM_DISABLED=-1001,I9100_PWM_BUSY=-1002,I9100_PWM_IO=-1003,I9100_PWM_CLOCK=-1004};
struct i9100_pwm_io {void *cookie;uint32_t (*read32)(void *,unsigned);void (*write32)(void *,unsigned,uint32_t);int (*lock)(void *);void (*unlock)(void *);};
struct i9100_pwm_haptic {
 struct i9100_pwm_io io;uint32_t initialized,enabled,prepared,running,fault,clock_hz,prescaler;
 uint32_t saved_control,saved_mux,saved_count,saved_compare,count,compare,actual_period_ns,actual_duty_ns;
};
int i9100_pwm_haptic_init(struct i9100_pwm_haptic *,const struct i9100_pwm_io *,uint32_t verified_pclk_hz);
int i9100_pwm_haptic_enable(struct i9100_pwm_haptic *,int);
int i9100_pwm_haptic_configure(struct i9100_pwm_haptic *,uint32_t period_ns,uint32_t duty_ns,int gpio_clock_lease_verified);
int i9100_pwm_haptic_run(struct i9100_pwm_haptic *,int);
int i9100_pwm_haptic_release(struct i9100_pwm_haptic *);
/* Exynos4210 PWM1@139D0000 only.32-bit counter and mux0=/1, preserve the
 * shared timer0/1 prescaler and all other timer control/mux bits. Initial
 * waveform constrained to LRA zero-drive50% through55% duty; no full power.
 * PWM period remains distinct from motor pulse duration(max250ms) and the
 * provider's timeout/PMIC rail ownership. Stop does not drive GPIO itself. */
#ifdef __cplusplus
}
#endif
#endif
