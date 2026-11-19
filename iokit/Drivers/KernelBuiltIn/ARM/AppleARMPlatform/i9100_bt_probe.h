/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BT_PROBE_H
#define I9100_BT_PROBE_H
#include "i9100_bt_boot.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_bt_probe_phase {I9100_BP_PREPARED=1,I9100_BP_VERSION,I9100_BP_NAME,
 I9100_BP_ADDRESS,I9100_BP_BUFFER,I9100_BP_FEATURES,I9100_BP_COMPLETE,I9100_BP_FAILED,I9100_BP_CLOSED};
struct i9100_bt_probe_io {
 struct i9100_bt_io h4;void *cookie;
 int (*read)(void *,uint8_t *,unsigned,unsigned *);
 int (*verify_link)(void *,uint32_t fresh_generation);
};
struct i9100_bt_probe_status {
 uint32_t initialized,enabled,phase,completed,fault,closed,generation,valid_fields;
 uint32_t packets,unsolicited,steps,address_unusable,last_event_bytes;
 uint16_t pending_opcode;struct i9100_bt_version version;
 uint8_t version_raw[9],name_raw[249],address_raw[7],buffer_raw[8],features_raw[9],last_event[257];
 uint64_t started_ns,last_ns;int32_t error,cleanup_error;
};
struct i9100_bt_probe {
 struct i9100_bt_probe_io io;struct i9100_bt_h4 h4;uintptr_t self;
 uint32_t timeout_ms,rx_used,rx_offset,frozen_steps;uint16_t deferred_opcode;
 uint32_t deferred_phase;uint64_t credit_started;
 uint8_t rx[256];struct i9100_bt_probe_status status;
};
int i9100_bt_probe_init(struct i9100_bt_probe *,int enabled,const struct i9100_bt_probe_io *,uint32_t fresh_generation,uint32_t timeout_ms);
int i9100_bt_probe_start(struct i9100_bt_probe *);
int i9100_bt_probe_step(struct i9100_bt_probe *,unsigned frame_budget);
int i9100_bt_probe_stop(struct i9100_bt_probe *);
int i9100_bt_probe_status(const struct i9100_bt_probe *,struct i9100_bt_probe_status *);
/* Default OFF. Fresh UART0/115200 electrical proof is supplied by its owner.
 * Only reads Local Version, Local Name, BDADDR, Buffer Size and Features.
 * No HCI reset, patchram/RAM write, address/baud/scan/LE change or RF action.
 * Exact raw replies, including a default/invalid address, are retained as
 * evidence; they never satisfy a reviewed firmware profile automatically.
 * A status/late/mismatched/partial reply or stale bytes quarantines this
 * generation; no flushing, repeated commands or synthetic controller values.
 */
#ifdef __cplusplus
}
#endif
#endif
