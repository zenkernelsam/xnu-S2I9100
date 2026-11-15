/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_BACKPLANE_PRIVATE_H
#define I9100_BCM_BACKPLANE_PRIVATE_H
#include "i9100_bcm4330.h"
/* Driver-internal register transactions. Caller holds the BCM semantic lock;
 * these do not claim recursively or grant a user client arbitrary MMIO.
 * Only the bounded enumeration/wrapper aperture is accepted here. A failed
 * window restore remains owned by bus->window_restore_pending until explicit
 * disable can restore it. No subsequent transfer may overwrite that evidence. */
int i9100_bcm_bp_read_locked(struct i9100_bcm4330 *,uint32_t,uint32_t *);
int i9100_bcm_bp_write_locked(struct i9100_bcm4330 *,uint32_t,uint32_t);
int i9100_bcm_bp_restore_locked(struct i9100_bcm4330 *);
#endif
