/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BT_WORKER_H
#define I9100_BT_WORKER_H
#include "i9100_bt_boot.h"
#include "i9100_radio_native.h"
#include "i9100_bcm_firmware.h"
#include "i9100_bt_probe.h"
#include "i9100_bt_acl.h"
#include "i9100_bt_stream.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_bt_worker;
struct i9100_bt_worker_profile {
 struct i9100_bt_boot_profile controller;
 uint32_t verified_uart_clock_hz,poll_us;enum i9100_uart_fraction fraction;
 uint32_t allow_host_reset;
};
struct i9100_bt_probe_worker_profile {
 uint32_t verified_uart_clock_hz,poll_us,timeout_ms;enum i9100_uart_fraction fraction;
};
struct i9100_bt_output {
 void *cookie;int (*receive)(void *,const struct i9100_bt_frame *);
};
struct i9100_bt_stream_consumer {
 void *cookie;void (*retain)(void *);void (*release)(void *);
 int (*receive)(void *,const uint8_t *,unsigned);
};
struct i9100_bt_worker_status {
 uint32_t ready,running,fault,closing,closed,uart_owned,session_hold,worker_hold,poll_us;
 uint64_t timer_ticks;struct i9100_bt_boot_status boot;int32_t error,cleanup_error;
 uint32_t probe_mode;struct i9100_bt_probe_status probe;
 uint32_t acl_ready;struct i9100_bt_acl_status acl;
 uint32_t stream_attached;struct i9100_bt_stream_status stream;
};
int i9100_bt_worker_create(int enabled,struct i9100_radio_native *,const struct i9100_bt_worker_profile *,
 const uint8_t *reviewed_hcd,uint32_t bytes,const struct i9100_bcm_fw_integrity *,
 const struct i9100_bt_output *,struct i9100_bt_worker **);
int i9100_bt_worker_create_probe(int enabled,struct i9100_radio_native *,const struct i9100_bt_probe_worker_profile *,struct i9100_bt_worker **);
int i9100_bt_worker_start(struct i9100_bt_worker *);
int i9100_bt_worker_command(struct i9100_bt_worker *,uint16_t opcode,const uint8_t *,unsigned bytes);
int i9100_bt_worker_acl_link(struct i9100_bt_worker *,uint16_t handle,struct i9100_bt_acl_link *);
int i9100_bt_worker_acl_send(struct i9100_bt_worker *,uint16_t handle,uint32_t link_generation,unsigned packet_boundary,
 const uint8_t *kernel_payload,unsigned bytes);
int i9100_bt_worker_stop(struct i9100_bt_worker *);
int i9100_bt_worker_stream_attach(struct i9100_bt_worker *,const struct i9100_bt_stream_consumer *);
int i9100_bt_worker_stream_open(struct i9100_bt_worker *,const void *kernel_client);
int i9100_bt_worker_stream_write(struct i9100_bt_worker *,const void *kernel_client,const uint8_t *,unsigned);
int i9100_bt_worker_stream_attributes(struct i9100_bt_worker *,const void *kernel_client,uint8_t *,unsigned,int set);
int i9100_bt_worker_stream_close(struct i9100_bt_worker *,const void *kernel_client);
int i9100_bt_worker_stream_detach(struct i9100_bt_worker *);
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
 * create_probe uses the same electrical and native lifecycle but sends only
 * five standard identity reads, with no HCD/version expectation. It stops
 * UART polling on completion and retains original reply bytes for audit.
 * A new physical generation is required before a subsequent bootstrap.
 * Boot mode owns an ACL link/credit controller, taking limits from actual
 * BR/LE buffer replies. RX events update credits before synchronous delivery;
 * host sends carry the current connection generation. Partial UART writes
 * keep outstanding counts and stop this session, without replay or release.
 * Optional stream consumer is retained until explicit detach, blocking destroy
 * while socket callbacks exist. Complete runtime frames go to one client only,
 * never to both original kernel consumer and socket. Disconnect closes the
 * stream and quiesces this physical generation; no silent reconnect/reset.
 * allow_host_reset is a separately default-OFF configuration selected before
 * bootstrap. A sole idle host Reset is sent once; the genuine completion is
 * delivered after real identity/buffer/feature requalification and ACL epoch
 * update. Physical supply generation, holds, HCD and nominal baud are retained.
 */
#ifdef __cplusplus
}
#endif
#endif
