/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PERIPHERAL_NATIVE_H
#define I9100_PERIPHERAL_NATIVE_H
#include "i9100_peripheral_board.h"
#include "i9100_peripheral_io.h"
#include "i9100_irq_registry.h"
#include "i9100_uart_bluetooth.h"
#include "i9100_bluetooth_h4.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_peripheral_native;
int i9100_peripheral_native_create(int enabled,struct i9100_peripheral_native **);
int i9100_peripheral_native_destroy(struct i9100_peripheral_native **);
struct i9100_gpio_broker *i9100_peripheral_native_gpio(struct i9100_peripheral_native *);
struct i9100_clock_gates *i9100_peripheral_native_clocks(struct i9100_peripheral_native *);
struct i9100_irq_registry *i9100_peripheral_native_irqs(struct i9100_peripheral_native *);
int i9100_peripheral_native_i2c_open(struct i9100_peripheral_native *,unsigned profile_index,uint32_t verified_pclk_hz,struct i9100_peripheral_i2c *);
int i9100_peripheral_native_i2c_close(struct i9100_peripheral_native *,unsigned profile_index);
int i9100_peripheral_native_uart_open(struct i9100_peripheral_native *,uint32_t verified_clock_hz,uint32_t baud,enum i9100_uart_fraction,int fresh_peer_gpio_clock_verified,struct i9100_bt_io *);
int i9100_peripheral_native_uart_read(struct i9100_peripheral_native *,uint8_t *,unsigned capacity,unsigned *received);
int i9100_peripheral_native_uart_close(struct i9100_peripheral_native *);
/* Actual IOKit private maps/locks/timebase bridge, no automatic registration.
 * enabled!=1 does no allocation or IO. No raw arbitrary-address mapping API.
 * I2C1/6/7 only, with separately verified PCLK and distinct outer bank-window
 * lock/controller lock. GPIO/I2C/clock operations reject IRQ-disabled context.
 * Active/failed leases keep their context alive; destroy never forces them.
 * Context owner serializes destroy with all API/borrowed-manager access.
 * GIC dispatch still belongs to the timer's single-IAR/single-EOI provider;
 * exposing the registry does not install an interrupt hook by itself. */
#ifdef __cplusplus
}
#endif
#endif
