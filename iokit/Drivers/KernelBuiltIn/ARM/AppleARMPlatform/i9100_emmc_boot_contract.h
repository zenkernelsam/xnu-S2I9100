/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_BOOT_CONTRACT_H
#define I9100_EMMC_BOOT_CONTRACT_H
#include <stdint.h>
#include "i9100_emmc_native.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_EMMC_BOOT_PROPERTY "i9100-emmc-boot"
#define I9100_EMMC_BOOT_BYTES 256U
#define I9100_EMMC_BOOT_REVIEWED 1U
#define I9100_EMMC_BOOT_MODEL 2U
struct i9100_emmc_volume_identity {
 uint32_t create_date;uint8_t finder_volume_id[8];
};
struct i9100_emmc_boot_contract {
 uint32_t flags;
 struct i9100_emmc_inherited_profile host;
 struct i9100_emmc_writer_identity card;
 struct i9100_emmc_volume_identity system,data;
 uint8_t pair_manifest_sha256[32];
};
int i9100_emmc_boot_decode(const uint8_t *,unsigned bytes,struct i9100_emmc_boot_contract *);
int i9100_emmc_boot_encode(const struct i9100_emmc_boot_contract *,uint8_t out[I9100_EMMC_BOOT_BYTES]);
int i9100_emmc_boot_match(const struct i9100_emmc_boot_contract *,const struct i9100_emmc_native_snapshot *);
/* Version 2 pointer-free LE record is exactly 256B; explicit board root
 * frequencies occupy offsets188/192. Version1 lacked these prerequisites
 * and must be regenerated from original evidence, never silently upgraded.
 * CRC32 catches partial DT
 * storage, not authenticity. REVIEWED is host review metadata, not a user
 * write/deployment approval. Model records can be inspected offline but
 * cannot enable the real IOKit boot owner. The pair SHA binds the image
 * preparation/deployment evidence; runtime compares stable HFS volume IDs
 * and current card/table, not a falsely advertised full-volume SHA check. */
#ifdef __cplusplus
}
#endif
#endif
