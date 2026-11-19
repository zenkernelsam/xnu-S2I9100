/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BT_BOOT_H
#define I9100_BT_BOOT_H
#include "i9100_bluetooth_h4.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_BT_HCD_MAX (1024U*1024U)
enum i9100_bt_boot_error {I9100_BT_BOOT_ARGUMENT=-1960,I9100_BT_BOOT_DISABLED=-1961,
 I9100_BT_BOOT_BUSY=-1962,I9100_BT_BOOT_OWNER=-1963,I9100_BT_BOOT_HASH=-1964,
 I9100_BT_BOOT_LAYOUT=-1965,I9100_BT_BOOT_ID=-1966,I9100_BT_BOOT_CLOCK=-1967,
 I9100_BT_BOOT_IO=-1968,I9100_BT_BOOT_FAULT=-1969,I9100_BT_BOOT_TIMEOUT=-1970};
enum i9100_bt_boot_phase {I9100_BT_BOOT_PREPARED=1,I9100_BT_BOOT_RESET,
 I9100_BT_BOOT_RESET_DELAY,I9100_BT_BOOT_ROM_VERSION,I9100_BT_BOOT_MINIDRV,
 I9100_BT_BOOT_MINIDRV_DELAY,I9100_BT_BOOT_RECORD,I9100_BT_BOOT_LAUNCH_DELAY,
 I9100_BT_BOOT_POST_RESET,I9100_BT_BOOT_POST_RESET_DELAY,I9100_BT_BOOT_VERSION,
 I9100_BT_BOOT_ADDRESS,I9100_BT_BOOT_BUFFER,I9100_BT_BOOT_FEATURES,I9100_BT_BOOT_SCAN_OFF,
 I9100_BT_BOOT_READY,I9100_BT_BOOT_FAILED,I9100_BT_BOOT_CLOSED};
struct i9100_bt_version {uint8_t hci_version,lmp_version;uint16_t hci_revision,manufacturer,lmp_subversion;};
struct i9100_bt_boot_profile {
 uint32_t board_data_reviewed,ram_base,ram_bytes,launch_address,timeout_ms;
 struct i9100_bt_version rom,patched;
 uint8_t hcd_sha256[32],factory_bdaddr_hci[6]; /* HCI wire order, not Ethernet MAC rules */
};
struct i9100_bt_hcd_record {uint32_t offset,bytes;uint16_t opcode;uint8_t payload_bytes,reserved;uint8_t sha256[32];};
struct i9100_bt_boot_io {
 struct i9100_bt_io h4;
 void *cookie;
 int (*read)(void *,uint8_t *,unsigned,unsigned *received);
 int (*verify_link)(void *,uint32_t fresh_generation);
 int (*sha256)(void *,const void *,uint32_t bytes,uint8_t digest[32]);
 int (*receive)(void *,const struct i9100_bt_frame *); /* synchronous bounded copy outside H4 lock */
};
struct i9100_bt_boot_status {
 uint32_t initialized,enabled,phase,ready,fault,closed,generation,records,record_index,confirmed_records;
 uint32_t mutation_possible,launch_possible,rx_packets,controller_errors,acl_mtu,acl_packets,sco_mtu,sco_packets;
 uint16_t pending_opcode;
 uint8_t features[8],address[6];struct i9100_bt_version observed_rom,observed_patched;
 uint64_t started_ns,last_ns,delay_until_ns;int32_t error,cleanup_error;
};
struct i9100_bt_boot {
 struct i9100_bt_boot_io io;struct i9100_bt_h4 h4;struct i9100_bt_boot_profile profile;
 const uint8_t *hcd;const struct i9100_bt_hcd_record *index;uintptr_t self;
 uint32_t hcd_bytes,optional_minidrv,record_cursor,current_record_bytes,rx_used,rx_offset,stalled_steps;
 uint32_t deferred_phase,deferred_bytes;uint16_t deferred_opcode;uint64_t credit_started;
 uint8_t deferred_payload[255],rx[256];struct i9100_bt_boot_status status;
};
int i9100_bt_hcd_plan(const uint8_t *,uint32_t bytes,const struct i9100_bt_boot_profile *,uint32_t *records);
int i9100_bt_hcd_audit(const uint8_t *,uint32_t bytes,const struct i9100_bt_boot_profile *,
 int (*sha256)(void *,const void *,uint32_t,uint8_t[32]),void *cookie,
 struct i9100_bt_hcd_record *,uint32_t capacity,uint32_t *records);
int i9100_bt_boot_init(struct i9100_bt_boot *,int enabled,const struct i9100_bt_boot_io *,
 const struct i9100_bt_boot_profile *,const uint8_t *private_hcd,uint32_t bytes,
 const struct i9100_bt_hcd_record *private_index,uint32_t records,uint32_t fresh_generation);
int i9100_bt_boot_start(struct i9100_bt_boot *);
int i9100_bt_boot_step(struct i9100_bt_boot *,unsigned frame_budget);
int i9100_bt_boot_command(struct i9100_bt_boot *,uint16_t standard_hci_opcode,const uint8_t *,unsigned bytes);
int i9100_bt_boot_stop(struct i9100_bt_boot *);
int i9100_bt_boot_status(const struct i9100_bt_boot *,struct i9100_bt_boot_status *);
/* Default OFF, one reviewed fresh UART0/115200 physical generation. Bootstrap
 * does not configure baud, BDADDR, PCM, LE or radio power and never writes
 * flash. After ready the trusted kernel HCI sender may issue standard commands;
 * it excludes reset/host-reset and vendor operations, but is not an RF policy.
 * HCD copied privately by the native
 * owner; SHA whole file + individual commands, bounded RAM writes and exact
 * reviewed launch address. Optional leading FC2E is consumed only once.
 * Synchronous setup advances only on exact successful COMMAND_COMPLETE;
 * COMMAND_STATUS is acceptance, never setup completion. Controller version,
 * address and buffer/features are actual replies, not predicted values.
 * Unknown completions, late ACKs, corruption, partial I/O and clock failure
 * quarantine. No automatic resend/reset/fallback. Stop quiesces only UART;
 * mutation/launch possible flags remain until an external verified chip reset.
 * This builds the real bootstrap/HCI receive foundation, not iOS discovery,
 * pairing, ACL credit handling, host-wake/LPM or RF/electrical verification.
 */
#ifdef __cplusplus
}
#endif
#endif
