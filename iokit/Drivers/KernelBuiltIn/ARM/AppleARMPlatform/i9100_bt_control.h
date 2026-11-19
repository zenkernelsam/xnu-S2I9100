/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BT_CONTROL_H
#define I9100_BT_CONTROL_H
#include "i9100_bt_worker.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_bt_control;
struct i9100_bt_control_status {
 uint32_t registered,closing,client_active,client_unit,client_pid,attempted_connects,accepted_connects;
 uint64_t client_uniqueid,rx_enqueued,rx_backpressure;
 int32_t error,cleanup_error;
};
int i9100_bt_control_create(int enabled,struct i9100_bt_worker *,struct i9100_bt_control **);
int i9100_bt_control_status(struct i9100_bt_control *,struct i9100_bt_control_status *);
int i9100_bt_control_destroy(struct i9100_bt_control **);
/* Default OFF; dynamic kernel control com.apple.uart.bluetooth, SOCK_STREAM.
 * Only the original BTServer UUID/mobile501 identity may connect. It binds one
 * kernel token/unique process generation per physical worker, uses actual
 * H4 queue/44B ARM32 termios and never fabricates controller responses. Socket
 * close/death quiesces the native worker. Failed deregister/stop retains the
 * control, socket token, worker consumer and supplier holds until explicit
 * cleanup succeeds. There is no driver/daemon/reset/baud/RF registration in
 * production platform files, and no tty/btwake/DTree publication here. */
#ifdef __cplusplus
}
#endif
#endif
