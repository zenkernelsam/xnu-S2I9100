/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_GPIO_BROKER_H
#define I9100_GPIO_BROKER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_GPIO_BANKS_MAX 40U
enum i9100_gpio_error { I9100_GPIO_OK=0,I9100_GPIO_ARGUMENT=-200,
 I9100_GPIO_DISABLED=-201,I9100_GPIO_BUSY=-202,I9100_GPIO_OWNER=-203,
 I9100_GPIO_RESERVED=-204,I9100_GPIO_EXHAUSTED=-205,I9100_GPIO_IO=-206 };
enum i9100_gpio_release_policy { I9100_GPIO_RESTORE=0,I9100_GPIO_KEEP=1,I9100_GPIO_HIGH_Z=2 };
struct i9100_gpio_io {
 void *cookie;
 uint32_t (*read32)(void *,unsigned region,unsigned offset);
 void (*write32)(void *,unsigned region,unsigned offset,uint32_t);
 int (*lock)(void *); /* zero=acquired, IRQs remain enabled */
 void (*unlock)(void *);
};
struct i9100_gpio_bank_config { uint16_t offset;uint8_t region,pins,reserved_mask; };
struct i9100_gpio_bank {
 struct i9100_gpio_bank_config config;
 uint32_t owners[8];
};
struct i9100_gpio_broker {
 struct i9100_gpio_io io;
 struct i9100_gpio_bank banks[I9100_GPIO_BANKS_MAX];
 uint32_t generation,count,enabled;
};
struct i9100_gpio_lease {
 struct i9100_gpio_broker *broker;
 uint32_t generation,saved[4];
 unsigned bank,mask,active;
};
int i9100_gpio_broker_init(struct i9100_gpio_broker *,const struct i9100_gpio_io *,const struct i9100_gpio_bank_config *,unsigned);
int i9100_gpio_broker_enable(struct i9100_gpio_broker *,int);
int i9100_gpio_claim(struct i9100_gpio_broker *,unsigned bank,unsigned mask,struct i9100_gpio_lease *);
int i9100_gpio_input(struct i9100_gpio_lease *,unsigned pin);
int i9100_gpio_output(struct i9100_gpio_lease *,unsigned pin,unsigned value);
int i9100_gpio_function(struct i9100_gpio_lease *,unsigned pin,unsigned function);
int i9100_gpio_pull(struct i9100_gpio_lease *,unsigned pin,unsigned pull); /* 0:none,1:down,3:up */
int i9100_gpio_drive(struct i9100_gpio_lease *,unsigned pin,unsigned strength);
int i9100_gpio_level(struct i9100_gpio_lease *,unsigned pin,unsigned *);
int i9100_gpio_release(struct i9100_gpio_lease *,enum i9100_gpio_release_policy);
/* Broker/lease objects must initially be zeroed. Initialization is disabled
 * and does not read/write GPIO. Claims are masks,
 * not arbitrary MMIO requests. A board profile reserves active mainline pins.
 * Mux/pull/drive writes and restore are read back before releasing ownership.
 * DAT input levels are not an output latch; restore verifies DAT only for
 * saved GPIO-output pins, never for an input or peripheral-function pad. */
#ifdef __cplusplus
}
#endif
#endif
