/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SENSOR_WORKER_H
#define I9100_SENSOR_WORKER_H
#include "i9100_sensors.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_sensor_worker;
struct i9100_sensor_consumer_lease {
 struct i9100_sensor_worker *worker;uintptr_t self;uint32_t generation,owned;
};
struct i9100_sensor_worker_profile {
 uint32_t mask,pclk_hz[2],poll_us,board_and_clock_verified;
 struct i9100_sensor_config sensor[3]; /* indexed kind-1; explicit axis/offset */
};
struct i9100_sensor_worker_status {
 uint32_t ready,running,stopping,fault,closed,mask,queued,capacity;
 uint32_t bus_owned,worker_holds,device_holds,converting,busy;
 uint32_t active[3],sensor_fault[3],sequence[3],id[3];
 uint64_t produced,consumed,dropped,not_ready,overflows,timer_ticks;
 int32_t error,cleanup_error;
 uint32_t consumer_owned,consumer_generation;
};
int i9100_sensor_worker_create(int enabled,struct i9100_peripheral_native *,const struct i9100_sensor_worker_profile *,struct i9100_sensor_worker **);
int i9100_sensor_worker_start(struct i9100_sensor_worker *);
int i9100_sensor_worker_stop(struct i9100_sensor_worker *);
int i9100_sensor_worker_recover_and_stop(struct i9100_sensor_worker *);
int i9100_sensor_worker_read(struct i9100_sensor_worker *,struct i9100_sensor_sample *kernel_samples,uint32_t capacity,uint32_t *received);
int i9100_sensor_worker_status(struct i9100_sensor_worker *,struct i9100_sensor_worker_status *);
int i9100_sensor_worker_destroy(struct i9100_sensor_worker **);
int i9100_sensor_worker_consumer_get(struct i9100_sensor_worker *,struct i9100_sensor_consumer_lease *);
int i9100_sensor_worker_consumer_put(struct i9100_sensor_consumer_lease *);
int i9100_sensor_worker_consumer_start(struct i9100_sensor_consumer_lease *);
int i9100_sensor_worker_consumer_stop(struct i9100_sensor_consumer_lease *,int recover);
int i9100_sensor_worker_consumer_read(struct i9100_sensor_consumer_lease *,struct i9100_sensor_sample *,uint32_t,uint32_t *);
/* Default OFF; opt-in create owns I2C1 for K3DH/K3G and I2C7 for AK8975,
 * verifies identities and prepares devices. I2C6/audio and PMIC are untouched.
 * Real OSObject/IOWorkLoop/timer serializes control and polling. No IRQ hook,
 * no fabricated samples; bounded32 FIFO drops oldest on full and counts it.
 * Raw units/sequence/timestamps are preserved; this is not CoreMotion/HID.
 * Each selected device plus worker holds its bus until verified device stop.
 * Transfer faults stop polling and keep leases; explicit recover-and-stop
 * may restore only faulted owned I2C hosts after masked GIC/high-pad checks.
 * It never resumes sampling, resets a device or discards ownership early.
 * No automatic retry/restart. Stop closes the session; use a fresh create
 * for another run. Status/dequeue remain available after fault/close.
 * Failed stop/remove retains the object/parent. Owner serializes pointer
 * lifetime and destroy, and cannot destroy from this work thread.
 * Consumer lease is exclusive/address+generation-bound and retains OSObject.
 * Legacy read/start/stop/recover/destroy cannot steal an attached consumer.
 * Put requires complete device/host shutdown; stale/copied handles and output
 * aliases cannot access the worker or overwrite its lease. No auto detach. */
#ifdef __cplusplus
}
#endif
#endif
