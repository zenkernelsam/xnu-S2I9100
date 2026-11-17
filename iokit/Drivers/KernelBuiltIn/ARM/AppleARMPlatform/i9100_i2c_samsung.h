/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_I2C_SAMSUNG_H
#define I9100_I2C_SAMSUNG_H
#include "i9100_i2c_gpio.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_i2c_samsung_io {
 void *cookie;
 uint32_t (*read32)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *); void (*unlock)(void *);
 uint64_t (*now_ns)(void *); int (*delay_us)(void *,uint32_t);
};
struct i9100_i2c_samsung {
 struct i9100_i2c_samsung_io io;
 uint64_t started,timeout_ns;
 uint32_t pclk_hz,bus_hz,con,saved[4],poll_limit,initialized,enabled,exclusive,fault;
};
int i9100_i2c_samsung_init(struct i9100_i2c_samsung *,const struct i9100_i2c_samsung_io *,uint32_t pclk_hz,uint32_t bus_hz);
int i9100_i2c_samsung_enable(struct i9100_i2c_samsung *,int enabled,int exclusive);
int i9100_i2c_samsung_transfer(struct i9100_i2c_samsung *,const struct i9100_i2c_message *,unsigned,unsigned *completed);
/* Polling controller protocol; provider must own pins/clock and mask its GIC
 * source before exclusive enable (IRQEN creates IRQPEND even when polled).
 * Input PCLK is verified externally. A timeout quarantines the controller
 * until explicit disable; arbitration loss never generates a STOP on a bus
 * owned by another master. Initialization is disabled and performs no IO.
 * enabled/fault also retains partial host programming after enable/restore
 * readback failure. Disable confirms stable CON/STAT/ADD/LC fields before
 * clearing it; volatile pending/arbitration/NACK bits are not restore data.
 */
#ifdef __cplusplus
}
#endif
#endif
