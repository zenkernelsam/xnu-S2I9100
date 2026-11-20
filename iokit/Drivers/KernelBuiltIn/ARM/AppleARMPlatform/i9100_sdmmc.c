/* SPDX-License-Identifier: GPL-2.0-only
 * Exynos4210 HSMMC2: polled, read-only SD transfers.
 * Protocol baseline: xnu-S2I9100 6ec0eaf (i9100_sdhci.c).
 * Samsung clock feedback: Linux v3.0 mach-exynos4/setup-sdhci.c.
 * Never repair sector contents: only a completed, error-free transfer is valid.
 */
#include "i9100_sdmmc.h"
#include <kern/debug.h>

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#define R_BLKSIZE  0x04
#define R_BLKCNT   0x06
#define R_ARG      0x08
#define R_XFER     0x0c
#define R_CMD      0x0e
#define R_RESP0    0x10
#define R_BUF      0x20
#define R_PRESENT  0x24
#define R_HOST     0x28
#define R_POWER    0x29
#define R_CLK      0x2c
#define R_TIMEOUT  0x2e
#define R_RESET    0x2f
#define R_INTSTS   0x30
#define R_INTEN    0x34
#define R_SIGNAL   0x38
#define R_CTRL2    0x80
#define R_CTRL3    0x84
#define R_CTRL4    0x8c
#define P_CMD_INHIBIT  (1U << 0)
#define P_DAT_INHIBIT  (1U << 1)
#define P_DAT_ACTIVE   (1U << 2)
#define I_CMD_DONE     (1U << 0)
#define I_XFER_DONE    (1U << 1)
#define I_READ_READY   (1U << 5)
#define I_ERRORS       0xffff8000U
#define I_CMD_TIMEOUT  (1U << 16)
#define I_CMD_CRC      (1U << 17)
#define I_DATA_TIMEOUT (1U << 20)
#define I_DATA_CRC     (1U << 21)
#define RESP_NONE 0U
#define RESP_LONG 1U
#define RESP_SHORT 2U
#define RESP_BUSY 3U
#define CMD_CRC   (1U << 3)
#define CMD_INDEX (1U << 4)
#define CMD_DATA  (1U << 5)
#define CMD_ABORT (3U << 6)
#define R1_FLAGS (RESP_SHORT | CMD_CRC | CMD_INDEX)
#define R1_ERRORS 0xfdffe008U
#define R1_READY (1U << 8)
#define R1_STATE(x) (((x) >> 9) & 15U)
#define RESET_ALL 1U
#define RESET_LINES 6U

static uint32_t r32(struct i9100_sd_card *c, unsigned r)
{ return c->io.read32(c->io.cookie, r); }
static uint16_t r16(struct i9100_sd_card *c, unsigned r)
{ return c->io.read16(c->io.cookie, r); }
static uint8_t r8(struct i9100_sd_card *c, unsigned r)
{ return c->io.read8(c->io.cookie, r); }
static void w32(struct i9100_sd_card *c, unsigned r, uint32_t v)
{ c->io.write32(c->io.cookie, r, v); }
static void w16(struct i9100_sd_card *c, unsigned r, uint16_t v)
{ c->io.write16(c->io.cookie, r, v); }
static void w8(struct i9100_sd_card *c, unsigned r, uint8_t v)
{ c->io.write8(c->io.cookie, r, v); }
static void delay(struct i9100_sd_card *c, uint32_t us)
{ c->io.delay_us(c->io.cookie, us); }

static int set_clock(struct i9100_sd_card *c, uint32_t divider)
{
	uint32_t n;
	w16(c, R_CLK, 0);
	w16(c, R_CLK, (uint16_t)((divider << 8) | 1U));
	for (n = 0; n < c->io.poll_bound; n++) {
		if (r16(c, R_CLK) & 2U) break;
		delay(c, 1);
	}
	if (n == c->io.poll_bound) return I9100_SD_TIMEOUT;
	w16(c, R_CLK, (uint16_t)((divider << 8) | 5U));
	delay(c, 20);
	c->clock_div = divider;
	return I9100_SD_OK;
}

static uint32_t slower_clock(uint32_t divider)
{ return divider < 4U ? 4U : divider < 8U ? 8U : 128U; }

static int status_error(uint32_t status)
{
	if (status & (I_CMD_TIMEOUT | I_DATA_TIMEOUT)) return I9100_SD_TIMEOUT;
	if (status & (I_CMD_CRC | I_DATA_CRC)) return I9100_SD_CRC;
	return I9100_SD_IOERR;
}

static int reset_host(struct i9100_sd_card *c, uint8_t mask)
{
	uint32_t n;
	w8(c, R_RESET, mask);
	for (n = 0; n < c->io.poll_bound; n++) {
		if (!(r8(c, R_RESET) & mask)) {
			w32(c, R_INTSTS, 0xffffffffU);
			return I9100_SD_OK;
		}
		delay(c, 1);
	}
	return I9100_SD_TIMEOUT;
}

static int wait_idle(struct i9100_sd_card *c, uint32_t mask)
{
	uint32_t n;
	for (n = 0; n < c->io.poll_bound; n++) {
		uint32_t s = r32(c, R_INTSTS);
		if (s & I_ERRORS) return status_error(s);
		if (!(r32(c, R_PRESENT) & mask)) return I9100_SD_OK;
		delay(c, 10);
	}
	return I9100_SD_TIMEOUT;
}

static int wait_irq(struct i9100_sd_card *c, uint32_t mask)
{
	uint32_t n;
	for (n = 0; n < c->io.poll_bound; n++) {
		uint32_t s = r32(c, R_INTSTS);
		/* Errors take precedence even when READY/DONE is also set. */
		if (s & I_ERRORS) return status_error(s);
		if (s & mask) return I9100_SD_OK;
		delay(c, 10);
	}
	return I9100_SD_TIMEOUT;
}

static int command(struct i9100_sd_card *c, unsigned idx, uint32_t arg,
                   uint16_t flags, uint32_t resp[4])
{
	unsigned i;
	int rc;
	uint32_t mask = P_CMD_INHIBIT;
	if (resp) for (i = 0; i < 4; i++) resp[i] = 0;
	/* CMD12 must be allowed to stop an active data transfer. */
	if (idx != 12 && ((flags & CMD_DATA) || (flags & 3U) == RESP_BUSY))
		mask |= P_DAT_INHIBIT;
	rc = wait_idle(c, mask);
	if (rc) return rc;
	w32(c, R_INTSTS, 0xffffffffU);
	w32(c, R_ARG, arg);
	w16(c, R_CMD, (uint16_t)((idx << 8) | flags));
	rc = wait_irq(c, I_CMD_DONE);
	if (rc) return rc;
	w32(c, R_INTSTS, I_CMD_DONE);
	if (resp && (flags & 3U) != RESP_NONE) {
		resp[0] = r32(c, R_RESP0);
		if ((flags & 3U) == RESP_LONG)
			for (i = 1; i < 4; i++) resp[i] = r32(c, R_RESP0 + 4 * i);
	}
	if ((flags & 3U) == RESP_BUSY) {
		rc = wait_idle(c, P_DAT_INHIBIT);
		if (rc) return rc;
		w32(c, R_INTSTS, I_XFER_DONE);
	}
	return I9100_SD_OK;
}

static int command_r1(struct i9100_sd_card *c, unsigned idx, uint32_t arg,
                      uint16_t flags, uint32_t resp[4])
{
	int rc = command(c, idx, arg, flags, resp);
	if (!rc && (resp[0] & R1_ERRORS)) {
		kprintf("i9100 SD: CMD%u card error R1=%08x\n", idx, resp[0]);
		return I9100_SD_IOERR;
	}
	return rc;
}

void i9100_sd_init(struct i9100_sd_card *c, const struct i9100_sd_io *io)
{
	unsigned i;
	uint8_t *p = (uint8_t *)c;
	for (i = 0; i < sizeof(*c); i++) p[i] = 0;
	c->io = *io;
}

/* Stop the card as well as resetting host state before retrying a failed read. */
static int recover(struct i9100_sd_card *c)
{
	uint32_t r[4], n;
	int rc = reset_host(c, RESET_LINES);
	if (rc) return rc;
	rc = command(c, 12, 0, RESP_BUSY | CMD_CRC | CMD_INDEX | CMD_ABORT, r);
	if (rc && reset_host(c, RESET_LINES)) return I9100_SD_IOERR;
	for (n = 0; n < 100; n++) {
		rc = command_r1(c, 13, c->rca << 16, R1_FLAGS, r);
		if (rc) return rc;
		if ((r[0] & R1_READY) && R1_STATE(r[0]) == 4) return I9100_SD_OK;
		delay(c, 1000);
	}
	return I9100_SD_TIMEOUT;
}

static int read_block(struct i9100_sd_card *c, uint64_t lba, uint8_t *out)
{
	unsigned attempt, w;
	unsigned attempts = c->clock_div && c->clock_div < 128U && !c->calibrating ? 4U : 3U;
	uint32_t r[4];
	int rc = I9100_SD_IOERR;
	for (attempt = 0; attempt < attempts; attempt++) {
		rc = wait_idle(c, P_DAT_INHIBIT | P_DAT_ACTIVE);
		if (!rc) {
			w16(c, R_BLKSIZE, 512U);
			w16(c, R_BLKCNT, 1U);
			w16(c, R_XFER, (1U << 4) | (1U << 1));
			rc = command_r1(c, 17, c->sdhc ? (uint32_t)lba : (uint32_t)(lba << 9),
			                R1_FLAGS | CMD_DATA, r);
		}
		if (!rc) rc = wait_irq(c, I_READ_READY);
		if (!rc) {
			w32(c, R_INTSTS, I_READ_READY);
			for (w = 0; w < 128; w++) {
				uint32_t word = r32(c, R_BUF);
				/* Caller may supply a byte-aligned buffer. */
				out[w * 4] = (uint8_t)word;
				out[w * 4 + 1] = (uint8_t)(word >> 8);
				out[w * 4 + 2] = (uint8_t)(word >> 16);
				out[w * 4 + 3] = (uint8_t)(word >> 24);
			}
			rc = wait_irq(c, I_XFER_DONE);
		}
		if (!rc) rc = wait_idle(c, P_DAT_INHIBIT | P_DAT_ACTIVE);
		if (!rc) {
			w32(c, R_INTSTS, I_XFER_DONE);
			return I9100_SD_OK;
		}
		if (++c->read_errors <= 12U)
			kprintf("i9100 SD: read lba=%llu try=%u rc=%d int=%08x state=%08x\n",
			        (unsigned long long)lba, attempt + 1, rc,
			        r32(c, R_INTSTS), r32(c, R_PRESENT));
		if (recover(c)) break;
		if (!c->calibrating && c->clock_div && c->clock_div < 128U) {
			uint32_t divider = slower_clock(c->clock_div);
			if (set_clock(c, divider)) break;
			c->speed_fallbacks++;
			kprintf("i9100 SD: retry at slower divider=%u\n", divider);
		}
	}
	c->identity_check_pending = 1;
	return rc;
}

/* A failed request is not replayed by the file mapper. Before a subsequent
 * request, verify that the retained physical card is still the original CID.
 * Never probe a replacement or silently select a different root image. */
static int revalidate_card(struct i9100_sd_card *c)
{
 uint32_t r[4],cid[4];int rc=recover(c);
 if(!rc)rc=command(c,7,0,RESP_NONE,r); /* Deselect: an unaddressed CMD7 has no response. */
 if(!rc)rc=command(c,10,c->rca<<16,RESP_LONG|CMD_CRC,cid);
 if(!rc)for(unsigned n=0;n<4;n++)if(cid[n]!=c->cid[n]){rc=I9100_SD_NODEV;break;}
 if(!rc)rc=command_r1(c,7,c->rca<<16,RESP_BUSY|CMD_CRC|CMD_INDEX,r);
 if(!rc)rc=command_r1(c,13,c->rca<<16,R1_FLAGS,r);
 if(!rc && (!(r[0]&R1_READY)||R1_STATE(r[0])!=4))rc=I9100_SD_IOERR;
 if(rc){c->ready=0;return rc;}
 c->identity_check_pending=0;return I9100_SD_OK;
}

int i9100_sd_read_multi(struct i9100_sd_card *c, uint64_t lba, uint8_t *out, uint32_t count)
{
	uint32_t i;
	if (!c || !out || !count || count > UINT32_MAX / 512U) return I9100_SD_ARG;
	if (!c->ready) return I9100_SD_NODEV;
	if (lba >= c->sectors || count > c->sectors - lba ||
	    lba > 0xffffffffULL || count - 1ULL > 0xffffffffULL - lba ||
	    (!c->sdhc && lba + count > 0x800000ULL)) return I9100_SD_ARG;
	if(c->identity_check_pending){int rc=revalidate_card(c);if(rc)return rc;}
	/* CMD17 isolates every sector; no inherited Auto CMD12/streaming state. */
	for (i = 0; i < count; i++) {
		int rc = read_block(c, lba + i, out + (uint64_t)i * 512U);
		if (rc) return rc;
	}
	return I9100_SD_OK;
}

int i9100_sd_read512(struct i9100_sd_card *c, uint64_t lba, uint8_t out[512])
{ return i9100_sd_read_multi(c, lba, out, 1); }

int i9100_sd_probe(struct i9100_sd_card *c)
{
	uint32_t r[4], n, cs;
	uint32_t acmd_arg = 0x40ff8000U;
	uint8_t reference[512], verify[512], volume_reference[512];
	uint64_t volume_lba = 0;
	int rc;
	c->ready = 0;
	c->sectors = 0;
	rc = reset_host(c, RESET_ALL);
	if (rc) return rc;
	w16(c, R_CLK, 0);
	/* SELBASECLK=2 selects CMU sclk_mmc2, as on Linux Exynos4.
	 * RX feedback + clock hold, all four delay selects for <25MHz. */
	w32(c, R_CTRL2, (1U << 31) | (1U << 30) | (1U << 14) | (1U << 8) | (2U << 4));
	w32(c, R_CTRL3, (1U << 31) | (1U << 23) | (1U << 15) | (1U << 7));
	w32(c, R_CTRL4, (r32(c, R_CTRL4) & ~(3U << 16)) | (3U << 16));
	w8(c, R_POWER, 0x0fU);
	delay(c, 25000);
	rc = set_clock(c, 128U);
	if (rc) return rc;
	w8(c, R_TIMEOUT, 0x0eU);
	w32(c, R_INTEN, 0xffffffffU);
	w32(c, R_SIGNAL, 0);
	/* External GPX3 card detect; controller's internal CD is not wired. */
	w8(c, R_HOST, 0xc0U);
	kprintf("i9100 SD: strict PIO v2 clk=%04x ctrl2=%08x ctrl3=%08x\n",
	        r16(c, R_CLK), r32(c, R_CTRL2), r32(c, R_CTRL3));
	delay(c, 50000);
	rc = command(c, 0, 0, RESP_NONE, 0);
	if (rc) return rc;
	delay(c, 2000);
	rc = command(c, 8, 0x1aaU, R1_FLAGS, r);
	if (rc == I9100_SD_TIMEOUT) {
		/* Only a timeout denotes an SD 1.x card. CRC errors are not SDSC. */
		if (reset_host(c, RESET_LINES)) return I9100_SD_TIMEOUT;
		acmd_arg &= ~(1U << 30);
	} else if (rc) return rc;
	else if ((r[0] & 0xfffU) != 0x1aaU) return I9100_SD_IOERR;
	for (n = 0; n < 1500; n++) {
		rc = command_r1(c, 55, 0, R1_FLAGS, r);
		if (rc) return rc;
		if (!(r[0] & (1U << 5))) return I9100_SD_IOERR;
		rc = command(c, 41, acmd_arg, RESP_SHORT, r);
		if (rc) return rc;
		if (r[0] & (1U << 31)) break;
		delay(c, 1000);
	}
	if (n == 1500) return I9100_SD_TIMEOUT;
	c->ocr = r[0];
	c->sdhc = !!(r[0] & (1U << 30));
	rc = command(c, 2, 0, RESP_LONG | CMD_CRC, c->cid);
	if (rc) return rc;
	rc = command(c, 3, 0, R1_FLAGS, r);
	if (rc) return rc;
	if (r[0] & 0xe000U) return I9100_SD_IOERR; /* R6 error bits */
	c->rca = r[0] >> 16;
	if (!c->rca) return I9100_SD_IOERR;
	rc = command(c, 9, c->rca << 16, RESP_LONG | CMD_CRC, c->csd);
	if (rc) return rc;
	/* SDHCI omits the CRC/end byte of R136; CSD bit 8 is RESP0 bit 0. */
	cs = (c->csd[3] >> 22) & 3U;
	if (cs == 1U && c->sdhc) {
		uint32_t size = (c->csd[1] >> 8) & 0x3fffffU;
		c->sectors = ((uint64_t)size + 1U) * 1024U;
	} else if (!cs && !c->sdhc) {
		uint32_t size = ((c->csd[2] & 3U) << 10) | (c->csd[1] >> 22);
		uint32_t mult = (c->csd[1] >> 7) & 7U;
		uint32_t blen = (c->csd[2] >> 8) & 15U;
		if (blen < 9 || blen > 11) return I9100_SD_IOERR;
		c->sectors = ((uint64_t)size + 1U) << (mult + 2U + blen - 9U);
	} else return I9100_SD_IOERR;
	if (c->sectors < 32768ULL) return I9100_SD_IOERR;
	rc = command_r1(c, 7, c->rca << 16, RESP_BUSY | CMD_CRC | CMD_INDEX, r);
	if (rc) return rc;
	if (!c->sdhc) {
		rc = command_r1(c, 16, 512, R1_FLAGS, r);
		if (rc) return rc;
	}
	c->ready = 1;
	/* Repeated raw reads must agree before publishing the card. */
	rc = i9100_sd_read512(c, 0, reference);
	if (rc) goto fail;
	if (reference[510] != 0x55 || reference[511] != 0xaa) {
		kprintf("i9100 SD: invalid MBR signature %02x %02x; refusing corrupt media\n",
		        reference[510], reference[511]);
		rc = I9100_SD_IOERR;
		goto fail;
	}
	for (n = 0; n < 2; n++) {
		unsigned i;
		rc = i9100_sd_read512(c, 0, verify);
		if (rc) goto fail;
		for (i = 0; i < 512; i++) if (reference[i] != verify[i]) {
			kprintf("i9100 SD: unstable MBR byte=%u read=%u: %02x != %02x\n",
			        i, n + 2, reference[i], verify[i]);
			rc = I9100_SD_IOERR;
			goto fail;
		}
	}
	kprintf("i9100 SD: MBR 55 aa verified 3/3, sectors=%llu SDHC=%d\n",
	        (unsigned long long)c->sectors, c->sdhc);
	/* Keep slow-clock reference bytes before switching to transfer speed.
	 * Compare both MBR and a nonzero FAT VBR, without transforming either. */
	for (n = 0; n < 4; n++) {
		const uint8_t *e = reference + 446U + n * 16U;
		uint32_t start = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
		                 ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
		if ((e[4] == 0x0b || e[4] == 0x0c) && start && start < c->sectors) {
			volume_lba = start;
			rc = i9100_sd_read512(c, volume_lba, volume_reference);
			if (rc) goto fail;
			break;
		}
	}
	c->calibrating = 1;
	{
		static const uint32_t dividers[] = { 2U, 4U, 8U, 128U };
		unsigned choice, round, i;
		for (choice = 0; choice < sizeof(dividers) / sizeof(dividers[0]); choice++) {
			rc = set_clock(c, dividers[choice]);
			for (round = 0; round < 2 && !rc; round++) {
				rc = i9100_sd_read512(c, 0, verify);
				if (!rc) for (i = 0; i < 512; i++) if (verify[i] != reference[i]) {
					rc = I9100_SD_IOERR; break;
				}
				if (!rc && volume_lba) {
					rc = i9100_sd_read512(c, volume_lba, verify);
					if (!rc) for (i = 0; i < 512; i++) if (verify[i] != volume_reference[i]) {
						rc = I9100_SD_IOERR; break;
					}
				}
			}
			if (!rc) break;
			kprintf("i9100 SD: transfer clock verification failed divider=%u rc=%d\n", dividers[choice], rc);
		}
	}
	c->calibrating = 0;
	if (rc) goto fail;
	kprintf("i9100 SD: transfer divider=%u nominal=%u kHz, raw references verified\n",
	        c->clock_div, 50000U / (2U * c->clock_div));
	return I9100_SD_OK;
fail:
	c->calibrating = 0;
	c->ready = 0;
	return rc;
}
#endif
