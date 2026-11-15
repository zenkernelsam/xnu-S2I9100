/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_I2C_GPIO_H
#define I9100_I2C_GPIO_H
#include "i9100_gpio_broker.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_i2c_error { I9100_I2C_ARGUMENT=-240,I9100_I2C_TIMEOUT=-241,
 I9100_I2C_ADDRESS_NACK=-242,I9100_I2C_DATA_NACK=-243,
 I9100_I2C_ARBITRATION=-244,I9100_I2C_BUS=-245,I9100_I2C_BUSY=-246,I9100_I2C_IO=-247 };
struct i9100_i2c_timing {
 void *cookie;
 uint64_t (*now_ns)(void *);
 int (*delay_us)(void *,uint32_t); /* zero=success */
 uint32_t half_us,poll_bound;
 uint64_t stretch_ns,transfer_ns;
};
struct i9100_i2c_message {uint8_t address,read;uint16_t length;uint8_t *bytes;};
struct i9100_i2c_gpio {
 struct i9100_gpio_lease lease;
 struct i9100_i2c_timing timing;
 unsigned scl,sda,busy,valid;
 uint64_t started;
};
int i9100_i2c_gpio_init(struct i9100_i2c_gpio *,struct i9100_gpio_broker *,unsigned bank,unsigned sda,unsigned scl,const struct i9100_i2c_timing *);
int i9100_i2c_gpio_transfer(struct i9100_i2c_gpio *,const struct i9100_i2c_message *,unsigned count,unsigned *completed);
int i9100_i2c_gpio_recover(struct i9100_i2c_gpio *); /* explicit nine-clock recovery */
int i9100_i2c_gpio_destroy(struct i9100_i2c_gpio *);
/* No address scan, general-call broadcast, implicit retries or driven-high
 * I2C lines. A transaction owns both pins and holds only a bus busy token. */
#ifdef __cplusplus
}
#endif
#endif
