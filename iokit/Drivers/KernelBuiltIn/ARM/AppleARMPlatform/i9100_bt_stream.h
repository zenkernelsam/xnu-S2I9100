/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BT_STREAM_H
#define I9100_BT_STREAM_H
#include "i9100_bluetooth_h4.h"
#include "i9100_bt_acl.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_BT_STREAM_CAP 8202U
#define I9100_BT_STREAM_PACKETS 32U
#define I9100_BT_TERMIOS_BYTES 44U
enum i9100_bt_stream_error {
 I9100_BTS_ARGUMENT=-2160,I9100_BTS_DISABLED=-2161,I9100_BTS_BUSY=-2162,
 I9100_BTS_OWNER=-2163,I9100_BTS_AGAIN=-2164,I9100_BTS_FRAME=-2165,
 I9100_BTS_IO=-2166,I9100_BTS_FAULT=-2167,I9100_BTS_TIMEOUT=-2168,
 I9100_BTS_CLOCK=-2169,I9100_BTS_UNSUPPORTED=-2170
};
struct i9100_bt_stream_io {
 void *cookie;
 int (*verify)(void *,uint32_t physical_generation);
 uint64_t (*now_ns)(void *);
 int (*command)(void *,uint16_t,const uint8_t *,unsigned);
 int (*acl_link)(void *,uint16_t,struct i9100_bt_acl_link *);
 int (*acl_send)(void *,uint16_t,uint32_t,unsigned,const uint8_t *,unsigned);
 /* All bytes accepted, or AGAIN with none accepted. Socket backpressure is
  * retained locally and never causes an already submitted UART packet replay. */
 int (*receive)(void *,const uint8_t *,unsigned);
};
struct i9100_bt_stream_status {
 uint32_t initialized,enabled,owned,closing,closed,fault,generation,baud,tx_queued,rx_queued;
 uint64_t tx_accepted,tx_submitted,rx_accepted,rx_delivered,tx_discarded,rx_discarded;
 uint64_t tx_packets,rx_packets,last_ns;
 int32_t error;
 uint32_t host_reset_enabled,paused;
 uint32_t host_reset_pending;uint64_t host_reset_accepted_ns;
};
struct i9100_bt_stream {
 uintptr_t self,client;
 struct i9100_bt_stream_io io;
 struct i9100_bt_stream_status status;
 uint64_t tx_wait_ns,rx_wait_ns;
 uint32_t entered,stalled,records,reset_reply_queued;
 struct {uint32_t bytes,generation;uint16_t handle,reserved;} tx_record[I9100_BT_STREAM_PACKETS];
 uint8_t attributes[I9100_BT_TERMIOS_BYTES];
 uint8_t tx[I9100_BT_STREAM_CAP],rx[I9100_BT_STREAM_CAP];
};
int i9100_bt_stream_init(struct i9100_bt_stream *,int enabled,const struct i9100_bt_stream_io *,uint32_t physical_generation,uint32_t nominal_baud);
int i9100_bt_stream_open(struct i9100_bt_stream *,const void *kernel_client);
int i9100_bt_stream_allow_host_reset(struct i9100_bt_stream *,int enabled);
int i9100_bt_stream_pause(struct i9100_bt_stream *,int paused);
int i9100_bt_stream_write(struct i9100_bt_stream *,const void *kernel_client,const uint8_t *,unsigned);
int i9100_bt_stream_receive(struct i9100_bt_stream *,const struct i9100_bt_frame *);
int i9100_bt_stream_step(struct i9100_bt_stream *,unsigned packet_budget);
int i9100_bt_stream_get_attributes(struct i9100_bt_stream *,const void *kernel_client,uint8_t *,unsigned);
int i9100_bt_stream_set_attributes(struct i9100_bt_stream *,const void *kernel_client,const uint8_t *,unsigned);
int i9100_bt_stream_attribute_baud(const uint8_t *,unsigned,uint32_t *target_baud);
int i9100_bt_stream_commit_baud(struct i9100_bt_stream *,const void *kernel_client,const uint8_t *,unsigned,uint32_t expected_baud,uint32_t actual_confirmed_nominal_baud);
int i9100_bt_stream_close(struct i9100_bt_stream *,const void *kernel_client);
int i9100_bt_stream_abort(struct i9100_bt_stream *,int reason);
int i9100_bt_stream_status(const struct i9100_bt_stream *,struct i9100_bt_stream_status *);
/* Default OFF. Caller serializes all APIs in one real worker gate. The client
 * token is kernel memory, never an address supplied by userspace. One client
 * per physical generation; close cannot reinitialize or free controller buffers.
 * Actual complete H4 RX packets are copied with the type byte; arbitrary socket
 * fragments are assembled into standard non-reset commands or managed ACL TX.
 * No synthetic HCI replies, vendor passthrough, baud switching, SCO,
 * sleep/wake, pin/clock/power mutation or automatic controller resend. Queue
 * failure is sticky; the native owner must quiesce UART before releasing holds.
 * The 44B little-endian ARM32 termios admits only existing115200/8N1/RTSCTS.
 * Nominal baud is host configuration, not a measurement of the physical clock. */
/* host_reset_enabled is selected before open; its sole four-byte packet must
 * stand alone. Successful native submission pauses subsequent host writes;
 * the worker resumes only after real reset/buffer requalification. Default
 * remains OFF, no changes to vendor/baud/SCO permissions. */
#ifdef __cplusplus
}
#endif
#endif
