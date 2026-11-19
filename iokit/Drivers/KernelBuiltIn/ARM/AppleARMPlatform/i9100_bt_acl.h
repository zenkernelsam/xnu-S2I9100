/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BT_ACL_H
#define I9100_BT_ACL_H
#include "i9100_bt_boot.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_BT_ACL_LINKS 32U
enum i9100_bt_acl_error {I9100_BA_ARGUMENT=-2000,I9100_BA_DISABLED=-2001,I9100_BA_OWNER=-2002,
 I9100_BA_CREDIT=-2003,I9100_BA_FRAME=-2004,I9100_BA_IO=-2005,I9100_BA_FAULT=-2006,I9100_BA_LIMIT=-2007};
struct i9100_bt_acl_io {
 void *cookie;int (*verify)(void *);
 int (*send)(void *,const uint8_t *h4_packet,unsigned bytes,unsigned *sent);
};
struct i9100_bt_acl_link {
 uint32_t active,generation,pool,outstanding;uint16_t handle;uint8_t type,address_type,address[6];
};
struct i9100_bt_acl_status {
 uint32_t initialized,enabled,fault,closed,links,generation,mtu[2],packets[2],available[2],outstanding[2];
 uint64_t submitted,completed,discarded,received,ignored_completions;int32_t error;
};
struct i9100_bt_acl {
 struct i9100_bt_acl_io io;uintptr_t self;
 struct i9100_bt_acl_link links[I9100_BT_ACL_LINKS];struct i9100_bt_acl_status status;
 uint32_t le_buffers_valid;
};
int i9100_bt_acl_init(struct i9100_bt_acl *,int enabled,const struct i9100_bt_acl_io *,const struct i9100_bt_boot_status *actual_boot);
int i9100_bt_acl_receive(struct i9100_bt_acl *,const struct i9100_bt_frame *);
int i9100_bt_acl_link(struct i9100_bt_acl *,uint16_t handle,struct i9100_bt_acl_link *out);
int i9100_bt_acl_send(struct i9100_bt_acl *,uint16_t handle,uint32_t link_generation,unsigned packet_boundary,
 const uint8_t *kernel_payload,unsigned bytes);
int i9100_bt_acl_stop(struct i9100_bt_acl *);
int i9100_bt_acl_status(const struct i9100_bt_acl *,struct i9100_bt_acl_status *);
/* Default OFF, serialized by the actual HCI worker. A link is admitted only
 * by a successful original connection event; generation rejects queued host
 * payloads for a disconnected/reused handle. Available ACL buffers originate
 * from actual Read Buffer Size/Read LE Buffer Size replies and are consumed
 * BEFORE a potentially partial UART write. Only original Number Completed
 * Packets or successful disconnect recovers a buffer; no time-based credits.
 * Completed means the controller released a buffer, not peer delivery.
 * Multi-handle completion batches validate in full before changing credits;
 * unknown handles (including unmanaged SCO) are ignored, never grant credits.
 * Stop keeps all outstanding counts for diagnostics and invalidates sends;
 * it does not assert remote disconnect/reset or erase a partial transfer.
 * Unicast BR/EDR/LE ACL transport only; no L2CAP reassembly/pairing/SCO audio,
 * broadcast/ISO support or actual controller/RF/IOSerial acceptance claim.
 */
#ifdef __cplusplus
}
#endif
#endif
