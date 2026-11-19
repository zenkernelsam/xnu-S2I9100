/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_RADIO_NATIVE_H
#define I9100_RADIO_NATIVE_H
#include "i9100_radio_power.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_radio_native;
struct i9100_radio_supply {
 void *cookie;int (*get)(void *,unsigned *owned);int (*verify)(void *);int (*put)(void *);
};
int i9100_radio_native_open(int enabled,struct i9100_peripheral_native *,const struct i9100_radio_supply *,int eur_u1_verified,struct i9100_radio_native **);
int i9100_radio_native_set_power(struct i9100_radio_native *,enum i9100_radio_role,int on);
int i9100_radio_native_uart_open(struct i9100_radio_native *,uint32_t verified_clock_hz,uint32_t baud,enum i9100_uart_fraction,struct i9100_bt_io *);
int i9100_radio_native_uart_read(struct i9100_radio_native *,uint8_t *,unsigned,unsigned *);
int i9100_radio_native_uart_validate_baud(struct i9100_radio_native *,uint32_t physical_generation,uint32_t expected_baud,uint32_t target_baud);
int i9100_radio_native_uart_change_baud(struct i9100_radio_native *,uint32_t physical_generation,uint32_t expected_baud,uint32_t target_baud,int peer_complete_verified);
int i9100_radio_native_sdio_open(struct i9100_radio_native *,uint32_t verified_sclk_hz,struct i9100_sdio **,struct i9100_f2_io *);
int i9100_radio_native_transport_close(struct i9100_radio_native *,enum i9100_radio_role);
int i9100_radio_native_borrow(struct i9100_radio_native *,enum i9100_radio_role,struct i9100_native_transport_hold *);
int i9100_radio_native_status(struct i9100_radio_native *,struct i9100_radio_status *);
int i9100_radio_native_verify(struct i9100_radio_native *,enum i9100_radio_role,uint32_t expected_generation);
int i9100_radio_native_verify_uart_cleanup(struct i9100_radio_native *,uint32_t expected_generation);
int i9100_radio_native_close(struct i9100_radio_native **);
/* Default OFF, real broker + IOLock/MCT timebase/native-owned hosts, no new
 * raw MMIO map or automatic registration. The supply provider remains owned
 * and independently validates rails/LPO throughout; it is not a test flag.
 * Reserve the exact two lease addresses before GPIO writes; hold role power
 * before host open and release it only after owned host close. Error with
 * nonnull context preserves cleanup ownership. Child/H4/F2/core/FW users
 * must finish before transport_close; close never kills active borrowers.
 * borrow registers a native hold for an active UART/SDIO host. A session
 * holds one until firmware/H4 is halted; each workloop holds another until
 * all event sources are stopped/removed. Release via native hold_put.
 * A physical BT power generation permits one UART open attempt; closing H4
 * cannot turn the same ROM/partial-write epoch into a fresh initialization.
 * Close blocks new opens, confirms GPIO/supply shutdown, then unbinds and
 * frees context. Any failure keeps parent/provider/context alive. The owner
 * serializes context pointer lifetime/close against wrapper API calls.
 * This powers/holds ROM awake; no native F1/FW boot/network/HCI service or
 * electrical/radio validation has yet been delivered. */
#ifdef __cplusplus
}
#endif
#endif
