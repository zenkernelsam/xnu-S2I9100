/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_H
#define I9100_EMMC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_EMMC_PA 0x12550000U
#define I9100_EMMC_MMIO_SIZE 0x1000U
#define I9100_EMMC_SECTOR_SIZE 512U
#define I9100_EMMC_MAX_SECTORS 32U
enum i9100_emmc_error {
 I9100_EMMC_OK=0,I9100_EMMC_TIMEOUT=-1,I9100_EMMC_RESPONSE_CRC=-2,
 I9100_EMMC_DATA_CRC=-3,I9100_EMMC_IO=-4,I9100_EMMC_ARGUMENT=-5,
 I9100_EMMC_NO_DEVICE=-6,I9100_EMMC_UNSUPPORTED=-7,
 I9100_EMMC_CARD_ERROR=-8,I9100_EMMC_PARTITION=-9,I9100_EMMC_CLOCK=-10,
 I9100_EMMC_OWNER=-11,I9100_EMMC_BUSY=-12,I9100_EMMC_READBACK=-13,I9100_EMMC_DIRTY=-14
};
/* Samsung4210 MSHCI: 32-bit FIFO at0x100, old32-bit IDMAC register map.
 * These offsets intentionally do not describe Exynos3475's64-bit host. */
enum i9100_emmc_register {
 EM_CTRL=0x00,EM_PWREN=0x04,EM_CLKDIV=0x08,EM_CLKSRC=0x0c,
 EM_CLKENA=0x10,EM_TMOUT=0x14,EM_CTYPE=0x18,EM_BLKSIZ=0x1c,
 EM_BYTCNT=0x20,EM_INTMASK=0x24,EM_CMDARG=0x28,EM_CMD=0x2c,
 EM_RESP0=0x30,EM_RESP1=0x34,EM_RESP2=0x38,EM_RESP3=0x3c,
 EM_RINTSTS=0x44,EM_STATUS=0x48,EM_FIFOTH=0x4c,EM_VERID=0x6c,
 EM_TCBCNT=0x5c,EM_TBBCNT=0x60,
 EM_HCON=0x70,EM_UHS=0x74,EM_BMOD=0x80,EM_IDSTS=0x8c,
 EM_IDINTEN=0x90,EM_DATA=0x100
};
struct i9100_emmc_io {
 void *cookie;
 uint32_t (*read32)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);
 uint64_t (*now_ns)(void *);
 int (*delay_us)(void *,uint32_t); /* zero=success; nonzero=abort */
 uint32_t input_clock_hz; /* verified CIU rate BEFORE CLKDIV, AFTER CLKSEL */
 uint32_t max_read_clock_hz; /* zero keeps identification clock */
 uint32_t poll_bound; /* independent bound even if the clock stops */
};
struct i9100_emmc_card {
 struct i9100_emmc_io io;
 uint32_t ocr,rca,cid[4],csd[4],version,hcon,clock_div,clock_hz,legacy_max_hz;
 uint32_t last_command,last_rint,last_status,last_response,stage;
 uint64_t sectors,completed_sectors;
 uint8_t ext_csd[512];
 int ready,last_error;
 uintptr_t self;
 uint32_t generation;
 struct i9100_emmc_write_grant *write_grant;
};
struct i9100_emmc_write_grant {
 uintptr_t self,card;
 uint32_t generation,cid[4],enabled,poisoned,dirty,mutation_possible;
 uint32_t partition_role,words_submitted,ciu_bytes,host_bytes,syncs;
 uint64_t first_lba,sectors,completed_sectors;
 int first_error;
};
void i9100_emmc_init(struct i9100_emmc_card *,const struct i9100_emmc_io *);
int i9100_emmc_probe(struct i9100_emmc_card *);
int i9100_emmc_read(struct i9100_emmc_card *,uint64_t,uint32_t,uint8_t *);
int i9100_emmc_write(struct i9100_emmc_card *,struct i9100_emmc_write_grant *,uint64_t,uint32_t,const uint8_t *);
int i9100_emmc_synchronize(struct i9100_emmc_card *,struct i9100_emmc_write_grant *);
/* Read-only clients create no grant and still issue no write/CMD6. The
 * opt-in writer policy alone binds a grant after current CID + both GPTs
 * + PIT checks. The lower command gate independently hardcodes user-area
 * FACTORYFS/DATAFS bounds and exact grant/card/generation. CMD6 can only
 * flush an already enabled supported device cache, never select boot,
 * enable caching, change partition/bus/voltage or erase. Caller serializes
 * every card API, including read/probe, and keeps grant storage live. */
#ifdef __cplusplus
}
#endif
#endif
