/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_HOST_H
#define I9100_EMMC_HOST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_emmc_host_lease {uintptr_t self;uint32_t owned;};
int i9100_emmc_host_acquire(struct i9100_emmc_host_lease *);
int i9100_emmc_host_check(const struct i9100_emmc_host_lease *);
int i9100_emmc_host_release(struct i9100_emmc_host_lease *,int actual_controller_quiescent);
/* One software owner for DWMMC0 across the existing RO driver and native
 * persistent path. No MMIO. Lease address stays live until actual CMD/data
 * quiescence is proved by its driver. Copied/stale leases cannot release it.
 * This is not a claim on SDHCI2/3, pins, rail or a measured clock frequency. */
#ifdef __cplusplus
}
#endif
#endif
