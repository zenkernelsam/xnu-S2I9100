/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_BOOT_H
#define I9100_EMMC_BOOT_H
#ifdef __cplusplus
#include <IOKit/IOService.h>
#include "i9100_emmc_boot_contract.h"
struct i9100_emmc_boot_owner;
int i9100_emmc_boot_open(int enabled,IOService *platform,const uint8_t *contract,unsigned bytes,i9100_emmc_boot_owner **);
int i9100_emmc_boot_close(i9100_emmc_boot_owner **);
int i9100_emmc_boot_snapshot(i9100_emmc_boot_owner *,struct i9100_emmc_native_snapshot *);
bool i9100_emmc_root_selected();
void ios6i9100_register_emmc_boot(IOService *platform);
/* Optional PE hook only. No ordinary boot creates a host or maps other
 * peripherals. Explicit selected internal root requires a REVIEWED/non-model
 * DT contract and actual card/tables/HFS stable identities before publication.
 * Success pins platform, parent, host and raw media for the boot lifetime;
 * close cannot claim success while the published storage tree is alive.
 * Failed native cleanup preserves its owner; no automatic reopen/reprobe. */
#endif
#endif
