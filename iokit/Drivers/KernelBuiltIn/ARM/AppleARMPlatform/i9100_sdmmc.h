/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Samsung Galaxy S2 (GT-I9100) SDHCI MicroSD block storage driver for Darwin/XNU.
 * Exynos 4210 SDHCI Channel 2 @ 0x12530000.
 */

#ifndef IOS7_I9100_SDMMC_H
#define IOS7_I9100_SDMMC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I9100_SDMMC_PA		0x12530000U
#define I9100_SDMMC_SZ		0x1000U

#define I9100_SD_OK		0
#define I9100_SD_TIMEOUT	-1
#define I9100_SD_CRC		-2
#define I9100_SD_ARG		-3
#define I9100_SD_IOERR		-4
#define I9100_SD_NODEV		-5

struct i9100_sd_io {
	void *cookie;
	uint32_t (*read32)(void *, unsigned reg);
	void (*write32)(void *, unsigned reg, uint32_t v);
	uint16_t (*read16)(void *, unsigned reg);
	void (*write16)(void *, unsigned reg, uint16_t v);
	uint8_t (*read8)(void *, unsigned reg);
	void (*write8)(void *, unsigned reg, uint8_t v);
	int (*delay_us)(void *, uint32_t us);
	uint64_t (*now_ns)(void *, int dummy);
	uint32_t poll_bound;
};

struct i9100_sd_card {
	struct i9100_sd_io io;
	uint32_t rca;
	uint32_t ocr;
	uint32_t cid[4];
	uint32_t csd[4];
	uint64_t sectors;
	int sdhc;
	int ready;
	uint32_t read_errors;
	uint32_t clock_div, calibrating, speed_fallbacks;
	uint32_t identity_check_pending;
};

void i9100_sd_init(struct i9100_sd_card *c, const struct i9100_sd_io *io);
int i9100_sd_probe(struct i9100_sd_card *c);
int i9100_sd_read512(struct i9100_sd_card *c, uint64_t lba, uint8_t out[512]);
int i9100_sd_read_multi(struct i9100_sd_card *c, uint64_t lba, uint8_t *out, uint32_t nsectors);

#ifdef __cplusplus
}
#endif

#endif /* IOS7_I9100_SDMMC_H */
