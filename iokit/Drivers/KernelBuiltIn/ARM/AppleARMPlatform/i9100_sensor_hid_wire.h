/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SENSOR_HID_WIRE_H
#define I9100_SENSOR_HID_WIRE_H
#include "i9100_sensors.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_sensor_hid_error {I9100_SH_ARGUMENT=-1920,I9100_SH_DISABLED=-1921,
 I9100_SH_BUSY=-1922,I9100_SH_OWNER=-1923,I9100_SH_RANGE=-1924,
 I9100_SH_CLOCK=-1925,I9100_SH_FAULT=-1926};
struct i9100_sensor_hid_packet {uint8_t bytes[60];};
int i9100_sensor_hid_usage(uint32_t kind,uint32_t *event_type,uint32_t *page,uint32_t *usage);
int i9100_sensor_hid_encode(const struct i9100_sensor_sample *,uint64_t absolute_ticks,
 uint64_t service_sender,struct i9100_sensor_hid_packet *);
/* Original iOS6 IOKit a98ba9fefc7333e4a5a9169198848c62: 16-byte serialized
 * header + 44-byte axis data, event types13/20/21. No host packing assumptions.
 * CoreMotion4512d901170d32e7842e7fc1c519386b matches vendor pageff00 usages3/9/10.
 * Raw worker micro-g -> g, micro-degrees/s -> radians/s, nano-tesla -> microtesla,
 * all signed16.16 with overflow rejection. Timestamp is native absolute ticks,
 * NOT the worker's nanoseconds; the provider must supply the actual conversion.
 * No synthesized samples/subtype/quaternion/calibration or hardware activation.
 */
#ifdef __cplusplus
}
#endif
#endif
