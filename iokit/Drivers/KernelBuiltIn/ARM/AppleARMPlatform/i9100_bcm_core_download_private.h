/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_CORE_DOWNLOAD_PRIVATE_H
#define I9100_BCM_CORE_DOWNLOAD_PRIVATE_H
#include "i9100_bcm_core_download.h"
/* Caller holds F1 semantic lock and firmware/image ownership. */
int i9100_bcm_core_start_cm3_locked(struct i9100_bcm_chip *);
int i9100_bcm_core_quiesce_locked(struct i9100_bcm_chip *);
#endif
