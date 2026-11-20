/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_LAYOUT_H
#define I9100_EMMC_LAYOUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_EMMC_PARTITIONS 32U
enum i9100_layout_error { I9100_LAYOUT_OK=0,I9100_LAYOUT_IO=-100,
 I9100_LAYOUT_HEADER=-101,I9100_LAYOUT_CRC=-102,I9100_LAYOUT_GEOMETRY=-103,
 I9100_LAYOUT_ENTRY=-104,I9100_LAYOUT_OVERLAP=-105,I9100_LAYOUT_LIMIT=-106,
 I9100_LAYOUT_PIT=-107,I9100_LAYOUT_HFS=-108 };
struct i9100_layout_io {
 void *cookie;
 int (*read512)(void *,uint64_t,uint8_t[512]); /* zero=complete sector */
 uint64_t sectors;
};
struct i9100_emmc_partition {
 uint8_t type_guid[16],unique_guid[16];
 uint64_t start_lba,sectors,attributes;
 uint32_t gpt_number,pit_identifier;
 char name[37];
};
struct i9100_emmc_layout {
 uint8_t disk_guid[16];
 uint64_t sectors,first_usable,last_usable,primary_array,backup_array;
 uint32_t primary_crc,backup_crc,entries_crc,count,valid,pit_valid;
 struct i9100_emmc_partition partitions[I9100_EMMC_PARTITIONS];
};
struct i9100_hfs_volume {
 uint64_t bytes;
 uint32_t allocation_block,total_blocks,free_blocks,attributes,create_date,journal_info_block;
 uint8_t finder_volume_id[8];
 uint16_t signature,version;
};
int i9100_emmc_read_layout(const struct i9100_layout_io *,struct i9100_emmc_layout *);
int i9100_emmc_check_pit(const struct i9100_layout_io *,struct i9100_emmc_layout *);
int i9100_emmc_hfs_volume(const struct i9100_layout_io *,const struct i9100_emmc_partition *,struct i9100_hfs_volume *);
const struct i9100_emmc_partition *i9100_emmc_find_partition(const struct i9100_emmc_layout *,const char *);
/* All operations are bounded reads. No parser repairs or rewrites a table. */
#ifdef __cplusplus
}
#endif
#endif
