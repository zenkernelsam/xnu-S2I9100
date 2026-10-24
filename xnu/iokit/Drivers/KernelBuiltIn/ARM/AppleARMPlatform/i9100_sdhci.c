/* I9100 Exynos4210 SDHCI PIO card PAL. Direct port of the uniLoader
 * sdhci4210.c sequence that already initializes the QEMU SD model and
 * streams the kernel Image off it: host scan -> soft reset -> clock ->
 * CMD0/8 -> ACMD41 -> CMD2/3/9 -> CMD7 -> CMD16 -> PIO CMD17/18 reads.
 * Register offsets are factual SDHCI 2.0; no foreign code imported. */
#include "i9100_sdhci.h"

/* SDHCI register offsets */
#define R_BLKSIZE	0x04
#define R_BLKCNT	0x06
#define R_ARG		0x08
#define R_XFER		0x0C
#define R_CMD		0x0E
#define R_RESP0		0x10
#define R_RESP1		0x14
#define R_RESP2		0x18
#define R_BUF		0x20
#define R_PRESENT	0x24
#define R_HOST		0x28
#define R_POWER		0x29
#define R_CLK		0x2C
#define R_TIMEOUT	0x2E
#define R_SWRST		0x2F
#define R_INTSTS	0x30
#define R_ERRSTS	0x32
#define R_INTEN		0x34
#define R_VERSION	0xFE

#define P_CMD_INHIBIT	(1U << 0)
#define P_DATA_INHIBIT	(1U << 1)
#define P_CARD_INSERTED	(1U << 16)

#define I_CMD_DONE	(1U << 0)
#define I_XFER_DONE	(1U << 1)
#define I_BUF_RD_RDY	(1U << 5)	/* bit5, NOT bit4 — bit4 is buf-WR */
#define I_ERROR		(1U << 15)

#define CMD_RESP_NONE	0x0
#define CMD_RESP_R136	0x1
#define CMD_RESP_R48	0x2
#define CMD_RESP_R48B	0x3
#define CMD_CRC_EN	(1 << 3)
#define CMD_IDX_EN	(1 << 4)
#define CMD_DATA_EN	(1 << 5)

#define X_BCNT_EN	(1 << 1)
#define X_ACMD12_EN	(1 << 2)
#define X_READ		(1 << 4)
#define X_MULTI		(1 << 5)

static uint32_t rd32(struct i9100_sd_card *c, unsigned r)
{ return c->io.read32(c->io.cookie, c->host_pa, r); }
static uint16_t rd16(struct i9100_sd_card *c, unsigned r)
{ return c->io.read16(c->io.cookie, c->host_pa, r); }
static void wr32(struct i9100_sd_card *c, unsigned r, uint32_t v)
{ c->io.write32(c->io.cookie, c->host_pa, r, v); }
static void wr16(struct i9100_sd_card *c, unsigned r, uint16_t v)
{ c->io.write16(c->io.cookie, c->host_pa, r, v); }
static void wr8(struct i9100_sd_card *c, unsigned r, uint8_t v)
{ c->io.write8(c->io.cookie, c->host_pa, r, v); }

static uint32_t sd_cmd(struct i9100_sd_card *c, unsigned idx, uint32_t arg,
		       unsigned resp, unsigned extra)
{
	unsigned t;

	t = 100000;
	while (rd32(c, R_PRESENT) & P_CMD_INHIBIT)
		if (!--t)
			return ~0u;
	if (extra & CMD_DATA_EN) {
		t = 100000;
		while (rd32(c, R_PRESENT) & P_DATA_INHIBIT)
			if (!--t)
				return ~0u;
	}

	wr32(c, R_INTSTS, 0xffffffff);
	wr32(c, R_ARG, arg);
	wr16(c, R_CMD, (uint16_t)((idx << 8) | resp | extra));

	t = 200000;
	for (;;) {
		uint32_t s = rd32(c, R_INTSTS);
		if (s & I_ERROR) {
			wr32(c, R_INTSTS, s);
			return ~0u;
		}
		if (s & I_CMD_DONE)
			break;
		if (!--t)
			return ~0u;
	}
	return rd32(c, R_RESP0);
}

static void host_reset_clock(struct i9100_sd_card *c)
{
	unsigned t;

	wr8(c, R_SWRST, 0x01);
	t = 100000;
	while (rd16(c, R_SWRST) & 0x07)
		if (!--t)
			break;

	wr8(c, R_POWER, 0x0F);			/* 3.3V + power on */

	/* Divisor N=128 -> ~200kHz from a ~52MHz base, safe for init. */
	wr16(c, R_CLK, (0x80 << 8) | 0x0001);
	t = 100000;
	while (!(rd16(c, R_CLK) & 0x0002))
		if (!--t)
			break;
	wr16(c, R_CLK, (0x80 << 8) | 0x0005);	/* + SD clock enable */

	/* QEMU only sets INTSTS when the matching status-enable bit is
	 * on; we poll INTSTS, signal-enable stays off. */
	wr32(c, R_INTEN, 0xFFFFFFFF);

	wr8(c, R_TIMEOUT, 0x0E);
	wr8(c, R_HOST, 0x00);			/* 1-bit bus */
}

static int sd_read_blocks(struct i9100_sd_card *c, uint32_t lba,
			  unsigned count, void *buf)
{
	unsigned t, n;
	uint32_t *p = buf;

	wr16(c, R_BLKSIZE, 0x0200 | (7 << 12));
	if (count > 1) {
		/* CMD18 needs Auto CMD12 or the card keeps streaming and
		 * the next command times out. */
		wr16(c, R_BLKCNT, (uint16_t)count);
		wr16(c, R_XFER, X_BCNT_EN | X_READ | X_MULTI | X_ACMD12_EN);
		if (sd_cmd(c, 18, c->sdhc ? lba : lba * 512,
			   CMD_RESP_R48 | CMD_CRC_EN | CMD_IDX_EN,
			   CMD_DATA_EN) == ~0u)
			return I9100_SD_ERROR;
	} else {
		wr16(c, R_BLKCNT, 1);
		wr16(c, R_XFER, X_BCNT_EN | X_READ);
		if (sd_cmd(c, 17, c->sdhc ? lba : lba * 512,
			   CMD_RESP_R48 | CMD_CRC_EN | CMD_IDX_EN,
			   CMD_DATA_EN) == ~0u)
			return I9100_SD_ERROR;
	}

	for (n = 0; n < count; n++) {
		unsigned w;

		t = 400000;
		while (!(rd32(c, R_INTSTS) & I_BUF_RD_RDY)) {
			if (rd32(c, R_INTSTS) & I_ERROR)
				return I9100_SD_ERROR;
			if (!--t)
				return I9100_SD_TIMEOUT;
		}
		wr32(c, R_INTSTS, I_BUF_RD_RDY);
		for (w = 0; w < 128; w++)
			p[n * 128 + w] = rd32(c, R_BUF);
	}

	t = 400000;
	while (!(rd32(c, R_INTSTS) & I_XFER_DONE)) {
		if (rd32(c, R_INTSTS) & I_ERROR)
			return I9100_SD_ERROR;
		if (!--t)
			return I9100_SD_TIMEOUT;
	}
	wr32(c, R_INTSTS, I_XFER_DONE);
	return I9100_SD_OK;
}

int i9100_sd_read_multi(struct i9100_sd_card *c, uint64_t lba,
			uint8_t *out, uint32_t nsectors)
{
	while (nsectors) {
		unsigned chunk = nsectors > 8 ? 8 : nsectors;
		int rc;

		if (!c->ready)
			return I9100_SD_ERROR;
		rc = sd_read_blocks(c, (uint32_t)lba, chunk, out);
		if (rc != I9100_SD_OK)
			return rc;
		lba += chunk;
		out += chunk * 512;
		nsectors -= chunk;
	}
	return I9100_SD_OK;
}

int i9100_sd_read512(struct i9100_sd_card *c, uint64_t lba, uint8_t out[512])
{
	return i9100_sd_read_multi(c, lba, out, 1);
}

int i9100_sd_probe(struct i9100_sd_card *c)
{
	uint32_t ocr, r;
	unsigned h;

	for (h = 0; h < I9100_SDHCI_HOSTS; h++) {
		c->host_pa = I9100_SDHCI_BASE(h);
		if (rd32(c, R_PRESENT) & P_CARD_INSERTED)
			break;
	}
	if (h == I9100_SDHCI_HOSTS)
		return I9100_SD_NOCARD;

	host_reset_clock(c);

	if (!(rd32(c, R_PRESENT) & P_CARD_INSERTED))
		return I9100_SD_NOCARD;

	if (sd_cmd(c, 0, 0, CMD_RESP_NONE, 0) == ~0u)
		return I9100_SD_ERROR;

	/* CMD8: spec>=2.0 echo check; HCS decided by OCR CCS below. */
	(void)sd_cmd(c, 8, 0x1AA, CMD_RESP_R48 | CMD_CRC_EN | CMD_IDX_EN, 0);

	for (;;) {
		unsigned t = 300;

		if (sd_cmd(c, 55, 0, CMD_RESP_R48 | CMD_CRC_EN | CMD_IDX_EN, 0)
		    == ~0u)
			return I9100_SD_ERROR;
		ocr = sd_cmd(c, 41, 0x40300000, CMD_RESP_R48, 0);
		if (ocr & (1u << 31))
			break;
		if (!--t)
			return I9100_SD_ERROR;
		c->io.delay_us(c->io.cookie, 1000);
	}
	c->ocr = ocr;
	c->sdhc = !!(ocr & (1u << 30));

	if (sd_cmd(c, 2, 0, CMD_RESP_R136 | CMD_CRC_EN, 0) == ~0u)
		return I9100_SD_ERROR;
	r = sd_cmd(c, 3, 0, CMD_RESP_R48 | CMD_CRC_EN | CMD_IDX_EN, 0);
	if (r == ~0u)
		return I9100_SD_ERROR;
	c->rca = r & 0xFFFF0000;

	if (sd_cmd(c, 9, c->rca, CMD_RESP_R136 | CMD_CRC_EN, 0) == ~0u)
		return I9100_SD_ERROR;
	{
		/* R136 is shifted: rspreg[i] holds CSD bits [i*32+8..i*32+39]. */
		uint32_t c0 = rd32(c, R_RESP1);
		uint32_t c1 = rd32(c, R_RESP2);

		if (c->sdhc) {
			uint32_t csize = (c0 >> 8) & 0x3FFFFF;
			c->sectors = (uint64_t)(csize + 1) * 1024;
		} else {
			uint32_t csize = ((c1 & 3) << 10) | (c0 >> 22);
			uint32_t mult = (c0 >> 7) & 7;
			uint32_t blen = (c1 >> 8) & 0xF;

			c->sectors = (uint64_t)(csize + 1) << (mult + 2);
			if (blen > 9)
				c->sectors <<= blen - 9;
			else if (blen < 9)
				c->sectors >>= 9 - blen;
		}
	}

	if (sd_cmd(c, 7, c->rca, CMD_RESP_R48B | CMD_CRC_EN | CMD_IDX_EN, 0)
	    == ~0u)
		return I9100_SD_ERROR;
	if (sd_cmd(c, 16, 512, CMD_RESP_R48 | CMD_CRC_EN | CMD_IDX_EN, 0)
	    == ~0u)
		return I9100_SD_ERROR;

	c->ready = 1;
	return I9100_SD_OK;
}
