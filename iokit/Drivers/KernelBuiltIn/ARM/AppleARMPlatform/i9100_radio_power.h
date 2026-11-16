/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_RADIO_POWER_H
#define I9100_RADIO_POWER_H
#include "i9100_peripheral_board.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_radio_role {I9100_RADIO_WLAN=1,I9100_RADIO_BT=2};
enum i9100_radio_error {I9100_RADIO_ARGUMENT=-850,I9100_RADIO_DISABLED=-851,I9100_RADIO_BUSY=-852,
 I9100_RADIO_IO=-853,I9100_RADIO_NOT_READY=-854,I9100_RADIO_TIMEOUT=-855,I9100_RADIO_OWNER=-856};
struct i9100_radio_io {
 void *cookie;
 int (*lock)(void *);void (*unlock)(void *);
 uint64_t (*now_ns)(void *);int (*delay_us)(void *,uint32_t);
 int (*transports_idle)(void *,enum i9100_radio_role);
 int (*supply_get)(void *,unsigned *owned); /* error may still return owned=1 */
 int (*supply_verify)(void *);int (*supply_put)(void *);
};
struct i9100_radio_power {
 struct i9100_radio_io io;struct i9100_gpio_broker *gpio;struct i9100_gpio_lease pins[3];
 uintptr_t self;
 uint32_t initialized,enabled,ready,closing,fault,configured,supply_owned,stage;
 uint32_t powered[2],power_owned[2],generation[2],holds[2];
 int32_t error,cleanup_error;
};
struct i9100_radio_hold {
 struct i9100_radio_power *power;uintptr_t self;uint32_t role,generation,owned;
};
struct i9100_radio_status {
 uint32_t enabled,ready,closing,fault,configured,supply_owned,stage,pins_owned;
 uint32_t powered[2],power_owned[2],generation[2],holds[2];
 int32_t error,cleanup_error;
 uint32_t native_bound,native_closing,native_uart_owned,native_sdio_owned;
 int32_t native_error,native_cleanup_error;
};
int i9100_radio_power_init(struct i9100_radio_power *,int enabled,struct i9100_gpio_broker *,const struct i9100_radio_io *,int eur_u1_verified);
int i9100_radio_set_power(struct i9100_radio_power *,enum i9100_radio_role,int on);
int i9100_radio_hold_get(struct i9100_radio_power *,enum i9100_radio_role,struct i9100_radio_hold *);
int i9100_radio_hold_put(struct i9100_radio_hold *);
int i9100_radio_power_status(struct i9100_radio_power *,struct i9100_radio_status *);
int i9100_radio_power_close(struct i9100_radio_power *);
/* Default OFF. EUR U1 control pins only: GPL1.2 WLAN_EN, GPL0.4 BT_EN,
 * GPL1.0 BT_nRST, GPX3.1 BT_WAKE. No HWAKE/SDIO/UART/PMIC/clock writes.
 * An exclusive board reservation must fence native transport opens. Initially
 * every control must be low and GPIO input/output, otherwise release the
 * unmodified leases without changing a foreign device. Assert output-low
 * before supply_get, then require a held, independently verified rails/LPO
 * provider. A nominal32k constant is never treated as measurement.
 * WLAN settles200us; BT EN->20ms->WAKE/high+reset release->50ms, with real
 * monotonic-delay/level and supply checks. This is ROM/power control, not FW
 * boot/network or proof of no RF activity when later used on hardware.
 * Role holds+actual idle guard block power-off. Partial errors retain pin/
 * supply/power ownership; no automatic on/reset retries. Explicit close
 * blocks new holds, confirms all controls low, releases supply, then KEEP
 * releases GPIO leases leaving outputs low rather than reviving old firmware.
 * Parent/provider/hold objects stay owned until close succeeds; IRQ disabled
 * use is rejected by the native locks. Never call these routines in IRQ. */
#ifdef __cplusplus
}
#endif
#endif
