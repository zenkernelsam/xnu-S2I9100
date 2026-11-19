/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SENSOR_HID_QUEUE_H
#define I9100_SENSOR_HID_QUEUE_H
#include "i9100_sensor_hid_wire.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_sensor_hid_queue_io {
 void *cookie;void (*barrier)(void *);void (*copy)(void *,void *,const void *,uint32_t);
};
int i9100_sensor_hid_queue_push(volatile uint32_t *head,volatile uint32_t *tail,
 uint8_t *storage,uint32_t capacity,uint32_t expected_tail,const struct i9100_sensor_hid_packet *,
 const struct i9100_sensor_hid_queue_io *,uint32_t *notify,uint32_t *new_tail);
/* Exact IODataQueue entry60+4, capacity4096. Caller owns an allocation and
 * serializes its private expected_tail; reader head is bounded/aligned.
 * No copying shared-reader pointers, no guessed bytes or queue length. */
#ifdef __cplusplus
}
#endif
#endif
