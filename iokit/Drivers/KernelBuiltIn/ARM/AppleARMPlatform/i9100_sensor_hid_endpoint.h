/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SENSOR_HID_ENDPOINT_H
#define I9100_SENSOR_HID_ENDPOINT_H
#include "i9100_sensor_hid.h"
#ifdef __cplusplus
#include <IOKit/IOUserClient.h>
struct i9100_sensor_hid_endpoint;
struct i9100_sensor_hid_endpoint_status {
 uint32_t ready,running,closing,closed,reader_open,claimed,kind,expected_tail,last_valid;
 uint64_t forwarded,dropped,notify_ok,notify_busy,notify_error,no_port;int32_t error,cleanup_error;
};
int i9100_sensor_hid_endpoint_create(int enabled,i9100_sensor_hid *,uint32_t kind,uint64_t registry_sender,struct i9100_sensor_hid_endpoint **);
int i9100_sensor_hid_endpoint_start(struct i9100_sensor_hid_endpoint *);
int i9100_sensor_hid_endpoint_claim(struct i9100_sensor_hid_endpoint *,void *client);
int i9100_sensor_hid_endpoint_unclaim(struct i9100_sensor_hid_endpoint *,void *client);
IOReturn i9100_sensor_hid_endpoint_method(struct i9100_sensor_hid_endpoint *,void *client,uint32_t selector,IOExternalMethodArguments *);
IOReturn i9100_sensor_hid_endpoint_port(struct i9100_sensor_hid_endpoint *,void *client,mach_port_t,UInt32);
IOReturn i9100_sensor_hid_endpoint_map(struct i9100_sensor_hid_endpoint *,void *client,UInt32,IOOptionBits *,IOMemoryDescriptor **);
int i9100_sensor_hid_endpoint_status(struct i9100_sensor_hid_endpoint *,struct i9100_sensor_hid_endpoint_status *);
int i9100_sensor_hid_endpoint_destroy(struct i9100_sensor_hid_endpoint **);
/* Native IOHIDService user-client ABI: open0/close1/copy2, port0, memory0.
 * Default OFF; only the existing IOHIDEventService extension publishes this.
 * Own IOBufferMemoryDescriptor keeps a detached reader mapping alive; exact
 * 60-byte queue records, private tail, bounded reader head, no data replay.
 * Endpoint takes per-kind collector lease before it runs. Stop the collector
 * before endpoint destruction; failed stop/remove/claimed client retains it.
 */
#endif
#endif
