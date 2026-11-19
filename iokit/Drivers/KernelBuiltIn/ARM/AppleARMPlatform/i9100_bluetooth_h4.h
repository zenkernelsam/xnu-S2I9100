/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BLUETOOTH_H4_H
#define I9100_BLUETOOTH_H4_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_bt_error {I9100_BT_ARGUMENT=-760,I9100_BT_DISABLED=-761,I9100_BT_BUSY=-762,
 I9100_BT_IO=-763,I9100_BT_FRAME=-764,I9100_BT_TIMEOUT=-765,I9100_BT_UNMATCHED=-766,I9100_BT_CONTROLLER=-767};
struct i9100_bt_io {
 void *cookie;int (*lock)(void *);void (*unlock)(void *);
 int (*write)(void *,const uint8_t *,unsigned,unsigned *sent);
 int (*quiesce)(void *);uint64_t (*now_ns)(void *);
};
struct i9100_bt_frame {uint8_t type;const uint8_t *bytes;uint32_t length;};
struct i9100_bt_h4 {
 struct i9100_bt_io io;
 uintptr_t self;
 uint64_t command_started;
 uint32_t initialized,enabled,fault,link_generation,pending,used,needed,header,packets;
 uint16_t opcode,last_opcode;
 uint8_t credits,last_status,last_completion;
 uint8_t frame[4101];
};
int i9100_bt_init(struct i9100_bt_h4 *,const struct i9100_bt_io *);
int i9100_bt_enable(struct i9100_bt_h4 *,int enabled,uint32_t verified_fresh_link_generation);
int i9100_bt_command(struct i9100_bt_h4 *,uint16_t opcode,const uint8_t *,unsigned length);
int i9100_bt_acl_packet(struct i9100_bt_h4 *,const uint8_t *h4_packet,unsigned bytes);
int i9100_bt_feed(struct i9100_bt_h4 *,const uint8_t *,unsigned length,unsigned *consumed,struct i9100_bt_frame *);
int i9100_bt_poll(struct i9100_bt_h4 *);
int i9100_bt_hcd_validate(const uint8_t *,uint32_t bytes,uint32_t verified_ram_base,uint32_t verified_ram_bytes,uint32_t *records);
/* H4 command/event/ACL/SCO framing with a single command window, finite
 * packet sizes and no resend. COMMAND_STATUS acknowledges command acceptance,
 * not completion of an asynchronous Bluetooth operation. Returned frame
 * view remains valid only until the next feed call; caller serializes it.
 * A failed/partial UART write requires a new physical link generation; a
 * parser reset alone is not treated as controller resynchronization.
 * HCD validation is offline-only and never sends or downloads anything. */
#ifdef __cplusplus
}
#endif
#endif
