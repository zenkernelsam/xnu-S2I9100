/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_PUBLICATION_H
#define I9100_EMMC_PUBLICATION_H
#ifdef __cplusplus
#include <IOKit/IOService.h>
#include "i9100_emmc_native.h"
class IOS6I9100EMMCRaw;
bool ios6i9100_publish_emmc_storage(int enabled,IOService *platform,i9100_emmc_native *,IOS6I9100EMMCRaw **retained_raw);
/* Explicit platform owner must open the reviewed native DATA_RW context
 * first. This installs three native-scoped block/whole-pair/BSD personalities
 * without rematching unrelated existing services and then registers the RO
 * raw wrapper. Its family rejects preexisting generic block/DOS/GPT drivers.
 * On success
 * caller owns retained_raw, and must terminate the media tree before
 * releasing it/closing native. No PE/boot argument invokes it by default.
 * Original XNU property/ancestor matching is covered offline. Actual service
 * jobs, BSD devices and HFS mounts still need runtime acceptance. */
#endif
#endif
