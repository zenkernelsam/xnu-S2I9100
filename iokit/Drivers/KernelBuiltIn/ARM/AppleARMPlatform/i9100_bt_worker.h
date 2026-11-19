/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BT_WORKER_H
#define I9100_BT_WORKER_H
#include "i9100_bt_boot.h"
#include "i9100_radio_native.h"
#include "i9100_bcm_firmware.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_bt_worker;
struct i9100_bt_worker_profile {
 struct i9100_bt_boot_profile controller;
 uint32_t verified_uart_clock_hz,poll_us;enum i9100_uart_fraction fraction;
};
struct i9100_bt_output {
 void *cookie;int (*receive)(void *,const struct i9100_bt_frame *);
};
struct i9100_bt_worker_status {
 uint32_t ready,running,fault,closing,closed,uart_owned,session_hold,worker_hold,poll_us;
 uint64_t timer_ticks;struct i9100_bt_boot_status boot;int32_t error,cleanup_error;
};
int i9100_bt_worker_create(int enabled,struct i9100_radio_native *,const struct i9100_bt_worker_profile *,
 const uint8_t *reviewed_hcd,uint32_t bytes,const struct i9100_bcm_fw_integrity *,
 const struct i9100_bt_output *,struct i9100_bt_worker **);
int i9100_bt_worker_start(struct i9100_bt_worker *);
int i9100_bt_worker_command(struct i9100_bt_worker *,uint16_t opcode,const uint8_t *,unsigned bytes);
int i9100_bt_worker_stop(struct i9100_bt_worker *);
int i9100_bt_worker_status(struct i9100_bt_worker *,struct i9100_bt_worker_status *);
int i9100_bt_worker_destroy(struct i9100_bt_worker **);
/* Default OFF. Real OSObject/IOWorkLoop keeps private HCD/index; opens only
 * fresh powered UART0 at115200, holding session and callback native borrowers.
 * Checks live supply/BT_EN/nRST/WAKE and generation before every H4 operation.
 * Never powers rails, resets physical chip, writes flash or retries commands.
 * Fault stops timer and attempts UART quiesce; stop/detach failures retain
 * firmware copies, holds/radio/provider. Native host closes only after sources
 * removed and both borrowers released. Caller powers off/reset afterwards.
 * Output callback synchronously copies a single complete controller frame;
 * close/reentry from the callback is rejected. No IRQ/LPM/IOSerial/IOBluetooth
 * registration or unrestricted vendor opcode passthrough; not a pairing stack.
 */
#ifdef __cplusplus
}
#endif
#endif
