/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PERIPHERAL_IO_H
#define I9100_PERIPHERAL_IO_H
#include "i9100_i2c_gpio.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_peripheral_i2c {
 void *cookie;
 int (*transfer)(void *,const struct i9100_i2c_message *,unsigned,unsigned *completed);
 int (*claim)(void *); void (*release)(void *); /* bus mutex; IRQs stay enabled */
 uint64_t (*now_ns)(void *); int (*delay_us)(void *,uint32_t);
};
/* Per-device drivers use claim across bank-address + data transactions.
 * transfer must not recursively lock the same mutex. No implicit retry. */
#ifdef __cplusplus
}
#endif
#endif
