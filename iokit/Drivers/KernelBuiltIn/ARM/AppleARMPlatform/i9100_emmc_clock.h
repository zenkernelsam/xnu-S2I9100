/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_CLOCK_H
#define I9100_EMMC_CLOCK_H
#include <stdint.h>
#include "i9100_emmc.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_EMMC_CLOCK_BYTES 512U
#define I9100_EMMC_CLOCK_COUNT 40U
#define I9100_EMMC_CLOCK_MODEL 1U
enum {EC_HOST,EC_CMU,EC_GPIO,EC_GIC,EC_CHIP,EC_REGIONS};
enum i9100_emmc_clock_field {
 EC_CHIP_ID,EC_IP_GATE,EC_SOURCE_GATE,EC_SOURCE_MUX,EC_SOURCE_DIV,EC_DIV_STATUS,
 EC_TOP0,EC_TOP1,EC_CPU_SOURCE,EC_MPLL0,EC_MPLL1,EC_EPLL0,EC_EPLL1,EC_VPLL0,EC_VPLL1,
 EC_GPK0_CON,EC_GPK0_DAT,EC_GPK0_PUD,EC_GPK0_DRV,EC_GPK1_CON,EC_GPK1_PUD,EC_GPK1_DRV,EC_GIC_ENABLE,
 EC_CTRL,EC_POWER,EC_CLKDIV,EC_CLKSRC,EC_CLKENA,EC_CTYPE,EC_INTMASK,EC_CMD,EC_STATUS,EC_RINT,
 EC_VER,EC_HCON,EC_UHS,EC_BMOD,EC_IDSTS,EC_IDINTEN,EC_CLKSEL
};
struct i9100_emmc_clock_io {
 void *cookie;uint32_t (*read32)(void *,unsigned region,unsigned offset);uint64_t (*now_ns)(void *);
 uint32_t model_fixture;
};
struct i9100_emmc_clock_result {
 uint32_t selected_source,parent_hz,fsys_hz,ciu_hz,card_hz,cmu_div,cmu_pre,host_div;
 uint32_t idle,pins_legacy,source_stable,error_field;
 int error;
};
uint32_t i9100_emmc_clock_region(unsigned field);
uint32_t i9100_emmc_clock_offset(unsigned field);
int i9100_emmc_clock_capture(int enabled,const struct i9100_emmc_clock_io *,uint32_t xxti_hz,uint32_t xusbxti_hz,
 uint8_t out[I9100_EMMC_CLOCK_BYTES],struct i9100_emmc_clock_result *);
int i9100_emmc_clock_decode(const uint8_t *,unsigned,struct i9100_emmc_clock_result *);
int i9100_emmc_clock_rate(const struct i9100_emmc_clock_io *,uint32_t xxti_hz,uint32_t xusbxti_hz,
 struct i9100_emmc_clock_result *);
/* No write callback. Two bounded snapshots retain source/pins/host raw values
 * without reading FIFO DATA or clearing any interrupt. Decode checks actual
 *4210 PLL45/46 mux->two FSYS dividers->FIXED CIU/2->DW CLKDIV, not the later
 *Exynos CLKSEL[26:24] divider. Sources0/1/6/7/8 are established from actual
 *root/PLL registers; ambiguous HDMI/USB PHY parents remain unsupported.
 *Root frequencies are explicit reviewed board evidence, never measured by
 *this arithmetic. Caller owns the host/maps and provides process context.
 *Failed capture still publishes diagnostic CRC-valid bytes; never retries,
 *changes clocks/pins/voltage, starts MMC, flushes or formats a volume.
 *Record is512B LE with CRC, no pointers/authentication. Model never hardware. */
#ifdef __cplusplus
}
#endif
#endif
