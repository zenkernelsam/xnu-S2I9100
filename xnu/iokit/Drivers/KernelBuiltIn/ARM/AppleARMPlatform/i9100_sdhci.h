/* I9100 Exynos4210 SDHCI PIO PAL (hosts 0x12510000..0x12540000).
 * Ported from the proven uniLoader sdhci4210.c bring-up driver: same
 * register offsets, same init sequence, same CMD17/18 PIO read path
 * that already boots the kernel off QEMU's SD model. No foreign code. */
#ifndef IOS7_I9100_SDHCI_H
#define IOS7_I9100_SDHCI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_SDHCI_BASE(n) (0x12510000U + ((n) * 0x10000U))
#define I9100_SDHCI_HOSTS 4U
#define I9100_SD_OK 0
#define I9100_SD_TIMEOUT -1
#define I9100_SD_ERROR -2
#define I9100_SD_NOCARD -3
struct i9100_sd_io {
    void *cookie;
    uint32_t (*read32)(void *, uint32_t pa, unsigned reg);
    uint16_t (*read16)(void *, uint32_t pa, unsigned reg);
    void (*write32)(void *, uint32_t pa, unsigned reg, uint32_t v);
    void (*write16)(void *, uint32_t pa, unsigned reg, uint16_t v);
    void (*write8)(void *, uint32_t pa, unsigned reg, uint8_t v);
    void (*delay_us)(void *, uint32_t us);
};
struct i9100_sd_card {
    struct i9100_sd_io io;
    uint32_t host_pa;      /* winning host base */
    uint32_t rca, ocr, csd[4];
    uint64_t sectors;
    int sdhc, ready;
};
/* Probes all four hosts; first with P_CARD_INSERTED wins (QEMU may wire
 * -drive if=sd to any index). Returns I9100_SD_OK and fills c->host_pa. */
int i9100_sd_probe(struct i9100_sd_card *c);
/* One 512B-sector read; lba is a sector index (HCS handled inside). */
int i9100_sd_read512(struct i9100_sd_card *c, uint64_t lba, uint8_t out[512]);
int i9100_sd_read_multi(struct i9100_sd_card *c, uint64_t lba, uint8_t *out, uint32_t nsectors);
#ifdef __cplusplus
}
#endif
#endif
