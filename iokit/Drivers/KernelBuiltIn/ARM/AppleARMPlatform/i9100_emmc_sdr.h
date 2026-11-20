/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_SDR_H
#define I9100_EMMC_SDR_H
#include "i9100_emmc.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_emmc_sdr_phase {
 I9100_SDR_NEW=0,I9100_SDR_CLOCK_LOW=1,I9100_SDR_PADS=2,
 I9100_SDR_TIMING=3,I9100_SDR_CLASS=4,I9100_SDR_WIDTH=5,
 I9100_SDR_VERIFY_SLOW=6,I9100_SDR_CLOCK_FAST=7,I9100_SDR_ACTIVE=8,I9100_SDR_FAILED=9,
 I9100_SDR_RESTORE_WIDTH=10,I9100_SDR_RESTORE_TIMING=11,I9100_SDR_RESTORE_CLASS=12,I9100_SDR_RESTORED=13
};
struct i9100_emmc_sdr_plan {
 uint32_t board_reviewed,width,high_speed,target_hz,ciu_hz,vcc_mv;
 uint32_t cid[4];
};
struct i9100_emmc_sdr_board_ops {
 void *cookie;
 int (*check)(void *); /* Actual board/host ownership, clock and pad readback. */
 int (*prepare_pads)(void *,unsigned width); /* Only owned data pads; never eMMC_EN. */
};
struct i9100_emmc_sdr_session {
 uintptr_t self,card;
 uint32_t generation,attempted,verified,poisoned,phase,configuration_possible,cid[4];
 uint32_t width,high_speed,target_hz,clock_hz,clock_div,ciu_hz,power_class,original_hz,original_div,original_class;
 uint32_t expected_switch,switch_live,switches,failure_phase;
 uint64_t switch_timeout_ns;
 int error;
};
int i9100_emmc_sdr_apply(int enabled,struct i9100_emmc_card *,struct i9100_emmc_sdr_session *,
 const struct i9100_emmc_sdr_plan *,const struct i9100_emmc_sdr_board_ops *);
int i9100_emmc_sdr_restore(struct i9100_emmc_card *,struct i9100_emmc_sdr_session *);
/* Default off. Exactly one explicit 4/8-bit SDR transition before writer
 * grants/publication. No general MMC command API, automatic fallback,
 * DDR/HS200, voltage/CMU/boot partition/cache/erase changes. The command gate
 * recognizes only this address-bound session's immediate HS_TIMING,
 * POWER_CLASS and BUS_WIDTH writes; CMD6 cannot grant sector writes.
 * Current CID, board/pad/clock evidence and both slow/fast EXT_CSD readbacks
 * must agree. Failures poison the card and retain its owned resources.
 * Explicit close restores legacy width/timing/class at identification speed
 * only after all writer grants are closed. Failure does not retry/downgrade.
 * Session storage remains live with its card; callers serialize all APIs. */
#ifdef __cplusplus
}
#endif
#endif
