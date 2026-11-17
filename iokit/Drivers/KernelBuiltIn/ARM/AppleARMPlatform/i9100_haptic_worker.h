/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_HAPTIC_WORKER_H
#define I9100_HAPTIC_WORKER_H
#include "i9100_peripheral_native.h"
#include "i9100_power_shared.h"
#include "i9100_vibrator.h"
#include "i9100_pwm_haptic.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_haptic_worker;
struct i9100_haptic_worker_profile {uint32_t pclk_hz,board_and_clock_verified;};
struct i9100_haptic_worker_status {
 uint32_t ready,stopping,closed,fault,active,pwm_prepared,pwm_running,pwm_fault;
 uint32_t parent_held,pin_owned,clock_owned,power_owned,power_held;
 uint32_t period_ns,duty_ns,timer_events,pulses;uint64_t deadline_ns;
 int32_t error,cleanup_error;
};
int i9100_haptic_worker_create(int enabled,struct i9100_peripheral_native *,const struct i9100_haptic_worker_profile *,struct i9100_haptic_worker **);
int i9100_haptic_worker_pulse(struct i9100_haptic_worker *,uint32_t milliseconds,unsigned amplitude_permille);
int i9100_haptic_worker_stop(struct i9100_haptic_worker *);
int i9100_haptic_worker_status(struct i9100_haptic_worker *,struct i9100_haptic_worker_status *);
int i9100_haptic_worker_destroy(struct i9100_haptic_worker **);
/* Default OFF, EUR U1 PWM1 only: GPD0.1 AF2/no pull/driveLV1,
 * 139D0000, PCLK supplied independently, keep timer0/1 shared prescaler.
 * Requires paired guarded Power provider and disabled motor; no extra Q1
 * motor GPIO. OSObject/IOWorkLoop serializes pulse/stop/timer. Bounds remain
 * <=250ms and 50..55% duty, never the vendor full-amplitude default.
 * Pad stays input until count/mode readback; stop verifies PWM and motor,
 * then restores counter/pad/clock before releasing Power/parent references.
 * Failed stop/restore/remove retains the object. No restart after fault,
 * no I2C in an IRQ. Scheduling/physical waveform/hard-hang stop unverified.
 * Owner serializes pointer lifetime and destroy; no destroy on work thread. */
#ifdef __cplusplus
}
#endif
#endif
