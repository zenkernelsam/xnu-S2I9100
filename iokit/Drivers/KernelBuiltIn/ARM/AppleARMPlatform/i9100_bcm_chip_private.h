/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_CHIP_PRIVATE_H
#define I9100_BCM_CHIP_PRIVATE_H
#include "i9100_bcm_chip.h"
/* Caller holds bus semantic lock and an exclusive live chip owner. */
int i9100_bcm_chip_ram_guard_locked(struct i9100_bcm_chip *);
#endif
