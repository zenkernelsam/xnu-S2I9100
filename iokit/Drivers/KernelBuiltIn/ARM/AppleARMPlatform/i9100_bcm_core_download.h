/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_CORE_DOWNLOAD_H
#define I9100_BCM_CORE_DOWNLOAD_H
#include "i9100_bcm_chip.h"
#ifdef __cplusplus
extern "C" {
#endif
int i9100_bcm_core_download_prepare(struct i9100_bcm_chip *,int exclusive_wlan_cold_reset_verified);
int i9100_bcm_core_download_stop(struct i9100_bcm_chip *);
/* Opt-in BCM4330 AI/CM3/one D11/SOCRAM only; all wrappers come from DMP.
 * The caller owns the fresh board power/host lifetime and deliberately permits
 * a destructive WLAN RAM/core reset; an inherited firmware is not restored.
 * No PMU/global chip/flash/BT reset and no CPU/D11 reset release are offered.
 * Prepare leaves CM3 and D11 held reset, releases only SOCRAM reset, verifies
 * its clock, then permits the separate actual RAM-size probe/staging module.
 * Masked IOCTRL preserves other fields; RESETSTATUS and RESETCTRL waits are
 * bounded by both time and count. Lost writes/timeout keep ownership until
 * explicit stop can halt all three cores. Cleanup never restarts an old RAM
 * image or treats failed reads as proof of reset. FW/image/selector holders
 * block stop; tear down/abort them first. Default startup never calls this.
 * Model/register-ready does not prove real reset timings or FW execution. */
#ifdef __cplusplus
}
#endif
#endif
