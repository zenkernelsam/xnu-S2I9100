/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SENSOR_HID_H
#define I9100_SENSOR_HID_H
#include "i9100_sensor_hid_wire.h"
#include "i9100_sensor_worker.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_sensor_hid;
struct i9100_sensor_hid_consumer {
 struct i9100_sensor_hid *collector;uintptr_t self;uint32_t kind,generation,owned;
};
struct i9100_sensor_hid_hold {
 struct i9100_sensor_hid *collector;uintptr_t self;uint32_t generation,owned;
};
struct i9100_sensor_hid_clock {
 void *cookie;int (*absolute_ticks)(void *,uint64_t sample_ns,uint64_t *ticks);
};
struct i9100_sensor_hid_profile {
 uint32_t poll_us;uint64_t sender[3];struct i9100_sensor_hid_clock clock;
};
struct i9100_sensor_hid_record {
 struct i9100_sensor_hid_packet packet;uint32_t kind,sequence;
 uint64_t sample_ns,absolute_ticks;
};
struct i9100_sensor_hid_status {
 uint32_t ready,running,fault,closing,closed,worker_owned,mask,queued[3];
 uint32_t last_sequence[3];uint64_t last_ns[3],last_ticks[3];
 uint64_t produced,consumed,dropped,discarded,timer_ticks;
 int32_t error,cleanup_error;
 uint32_t consumer_mask;
 uint32_t service_owned;
};
void i9100_sensor_hid_native_clock(struct i9100_sensor_hid_clock *);
int i9100_sensor_hid_create(int enabled,struct i9100_sensor_worker *,const struct i9100_sensor_hid_profile *,struct i9100_sensor_hid **);
int i9100_sensor_hid_start(struct i9100_sensor_hid *);
int i9100_sensor_hid_stop(struct i9100_sensor_hid *);
int i9100_sensor_hid_recover_and_stop(struct i9100_sensor_hid *);
int i9100_sensor_hid_read(struct i9100_sensor_hid *,uint32_t kind,struct i9100_sensor_hid_record *,uint32_t capacity,uint32_t *received);
int i9100_sensor_hid_status(struct i9100_sensor_hid *,struct i9100_sensor_hid_status *);
int i9100_sensor_hid_destroy(struct i9100_sensor_hid **);
int i9100_sensor_hid_consumer_get(struct i9100_sensor_hid *,uint32_t kind,struct i9100_sensor_hid_consumer *);
int i9100_sensor_hid_consumer_put(struct i9100_sensor_hid_consumer *);
int i9100_sensor_hid_consumer_bind_sender(struct i9100_sensor_hid_consumer *,uint64_t registry_sender);
int i9100_sensor_hid_consumer_read(struct i9100_sensor_hid_consumer *,struct i9100_sensor_hid_record *,uint32_t capacity,uint32_t *received);
int i9100_sensor_hid_hold_get(struct i9100_sensor_hid *,struct i9100_sensor_hid_hold *);
int i9100_sensor_hid_hold_put(struct i9100_sensor_hid_hold *);
/* Default OFF, no platform registration or service impersonation. One actual
 * OSObject/workloop retains an exclusive worker lease; each selected kind has
 * 16 encoded records, dropping oldest on full. No DRDY/overflow fabrication,
 * sample replay or time-derived frames. Fault stops collection and attempts
 * real device shutdown; failed cleanup retains both objects and their leases.
 * Cached packets remain readable after stop/fault. Native absolute time and
 * reviewed per-service registry senders are required. This is the common
 * transport for future native IOHIDEventService endpoints, not a published
 * CoreMotion service. Worker/clock lifetime covers successful destroy. */
#ifdef __cplusplus
}
#endif
#endif
