/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PERIPHERAL_NATIVE_H
#define I9100_PERIPHERAL_NATIVE_H
#include "i9100_peripheral_board.h"
#include "i9100_peripheral_io.h"
#include "i9100_irq_registry.h"
#include "i9100_uart_bluetooth.h"
#include "i9100_bluetooth_h4.h"
#include "i9100_bcm_f2.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_peripheral_native;
enum i9100_native_transport_kind {I9100_NATIVE_UART=1,I9100_NATIVE_SDIO=2};
struct i9100_native_transport_lease {
 struct i9100_peripheral_native *context;uintptr_t self;
 uint32_t kind,generation,owned;
};
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
int i9100_peripheral_native_sdio_open(struct i9100_peripheral_native *,uint32_t verified_sclk_mmc3_hz,int exclusive_board_power_verified,struct i9100_sdio **,struct i9100_f2_io *);
int i9100_peripheral_native_sdio_close(struct i9100_peripheral_native *);
int i9100_peripheral_native_uart_open_owned(struct i9100_peripheral_native *,struct i9100_native_transport_lease *,uint32_t verified_clock_hz,uint32_t baud,enum i9100_uart_fraction,int verified,struct i9100_bt_io *);
int i9100_peripheral_native_uart_read_owned(struct i9100_native_transport_lease *,uint8_t *,unsigned,unsigned *);
int i9100_peripheral_native_sdio_open_owned(struct i9100_peripheral_native *,struct i9100_native_transport_lease *,uint32_t verified_sclk_hz,int verified,struct i9100_sdio **,struct i9100_f2_io *);
int i9100_peripheral_native_transport_close(struct i9100_native_transport_lease *);
int i9100_peripheral_native_transport_idle(struct i9100_peripheral_native *,enum i9100_native_transport_kind);
int i9100_peripheral_native_radio_bind(struct i9100_peripheral_native *,struct i9100_native_transport_lease *uart_owner,struct i9100_native_transport_lease *sdio_owner);
int i9100_peripheral_native_radio_unbind(struct i9100_peripheral_native *,struct i9100_native_transport_lease *uart_owner,struct i9100_native_transport_lease *sdio_owner);
/* Board reservation is software-only and requires both hosts idle. While
 * bound, only the two designated leases may open their hosts; even legacy
 * callbacks are excluded. The parent stays alive before GPIO claims exist.
 * Unbind requires the exact pair and both hosts fully closed. Power manager
 * completes its GPIO shutdown before unbinding; idle alone is not a fence. */
/* Owned variants tie callbacks and shutdown to a caller-held lease address
 * and host generation. Legacy APIs cannot close/read or borrow an owned
 * instance. Copy/stale leases and callbacks from closed instances are rejected.
 * An open error with lease.owned=1 is retained cleanup, never a ready IO;
 * transport_close is the only recovery and clears owned only after all leases
 * and controller state are restored. Close start blocks new protocol IO.
 * Keep this lease object and parent alive/unmodified until every borrower is
 * closed and transport_close succeeds; only then reuse/free its memory.
 * Idle is a read-only snapshot, not an exclusive ownership acquisition. */
/* SDHCI3 opens F1 only, never boots firmware/F2 or changes WLAN/BT rails.
 * The board owner has already verified rail/reset state and sclk_mmc3 input
 * (after its source/dividers). Close follows BCM/F2/workloop shutdown. */
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
