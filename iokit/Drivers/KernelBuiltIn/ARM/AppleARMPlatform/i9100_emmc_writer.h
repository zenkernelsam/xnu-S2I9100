/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_WRITER_H
#define I9100_EMMC_WRITER_H
#include "i9100_emmc.h"
#include "i9100_emmc_layout.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_emmc_write_role {I9100_EMMC_SYSTEM_VOLUME=1,I9100_EMMC_DATA_VOLUME=2};
struct i9100_emmc_writer_identity {
 uint32_t cid[4],primary_crc,backup_crc,entries_crc;
 uint8_t disk_guid[16],system_guid[16],data_guid[16];
 uint64_t sectors;
};
struct i9100_emmc_writer_io {
 void *cookie;
 int (*claim)(void *);
 void (*release)(void *);
 int (*verify_exclusive)(void *,const struct i9100_emmc_card *);
};
struct i9100_emmc_writer {
 uintptr_t self;
 struct i9100_emmc_card *card;
 struct i9100_emmc_writer_io io;
 struct i9100_emmc_writer_identity identity;
 struct i9100_emmc_write_grant grant;
 uint8_t staged[I9100_EMMC_MAX_SECTORS*I9100_EMMC_SECTOR_SIZE];
 uint8_t readback[I9100_EMMC_MAX_SECTORS*I9100_EMMC_SECTOR_SIZE];
 uint32_t initialized,opened,closing,role,verified_sectors;
 int error;
};
int i9100_emmc_writer_init(struct i9100_emmc_writer *,const struct i9100_emmc_writer_io *);
int i9100_emmc_writer_open(struct i9100_emmc_writer *,int enabled,struct i9100_emmc_card *,
 const struct i9100_emmc_writer_identity *,enum i9100_emmc_write_role,uint64_t first_lba,uint64_t sectors);
int i9100_emmc_writer_write(struct i9100_emmc_writer *,uint64_t relative_lba,uint32_t sectors,const uint8_t *);
int i9100_emmc_writer_synchronize(struct i9100_emmc_writer *);
int i9100_emmc_writer_close(struct i9100_emmc_writer *);
/* Default OFF, kernel-private writer; not an exported raw disk/user client.
 * Open reads CURRENT both GPTs and PIT through the actual card, compares a
 * separately approved CID/table identity and the fixed real-handset two
 * slice geometry. Grant can only cover an explicit window inside one slice.
 * All card traffic (including external readers/probe) shares claim/exclusive
 * ownership. Failed write/flush is poisoned and never retried; close then
 * refuses instead of declaring a clean volume or silently reprobing it.
 * This module is not yet attached to writable IOMedia/rootfs/reboot hooks. */
#ifdef __cplusplus
}
#endif
#endif
