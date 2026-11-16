/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_FIRMWARE_PRIVATE_H
#define I9100_BCM_FIRMWARE_PRIVATE_H
#include "i9100_bcm_firmware.h"
/* Bootstrap owner already holds the F1 semantic lock. */
int i9100_bcm_fw_abort_locked(struct i9100_bcm_firmware *);
#endif
