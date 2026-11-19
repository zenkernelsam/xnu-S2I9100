/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SENSOR_HID_SERVICE_H
#define I9100_SENSOR_HID_SERVICE_H
#include "i9100_sensor_hid.h"
#ifdef __cplusplus
#include <IOKit/IOService.h>
struct i9100_sensor_hid_service;
int i9100_sensor_hid_service_create(int enabled,IOService *platform,struct i9100_sensor_hid *,uint32_t mask,struct i9100_sensor_hid_service **);
int i9100_sensor_hid_service_status(struct i9100_sensor_hid_service *,uint32_t *published_mask);
int i9100_sensor_hid_service_destroy(struct i9100_sensor_hid_service **);
/* Implemented only by the explicit existing IOHIDEventService extension patch.
 * No second class with that original name, no boot registration. Setup borrows
 * one endpoint per selected sensor and binds actual registry entry IDs, then
 * publishes services; owner starts the collector afterwards. Destroy first
 * stops the collector, then waits for clients/endpoint sources/leases before
 * detach/release. Failure returns the retained session for explicit cleanup.
 */
#endif
#endif
