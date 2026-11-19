/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_PUBLICATION_H
#define I9100_EMMC_PUBLICATION_H
#ifdef __cplusplus
#include <IOKit/IOService.h>
#include "i9100_emmc_native.h"
class IOS6I9100EMMCRaw;
bool ios6i9100_publish_emmc_storage(int enabled,IOService *platform,i9100_emmc_native *,IOS6I9100EMMCRaw **retained_raw);
/* Explicit platform owner must open the reviewed native DATA_RW context
 * first. This installs ordinary block-storage/BSD personalities plus the
 * exact two-volume class and registers the RO raw wrapper. On success
 * caller owns retained_raw, and must terminate the media tree before
 * releasing it/closing native. No PE/boot argument invokes it by default.
 * Actual service matching, BSD devices and HFS mounts need hardware/runtime
 * acceptance; source compile alone does not prove registry discovery. */
#endif
#endif
