/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_CHIP_H
#define I9100_BCM_CHIP_H
#include "i9100_bcm4330.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_BCM_MAX_CORES 16U
#define I9100_BCM_EROM_WORDS (0xe00U/4U)
#define I9100_BCM_MAX_BANKS 31U
#define I9100_BCM_MAX_RAM (4U*1024U*1024U)
enum i9100_bcm_chip_error {I9100_BCM_CHIP_ARGUMENT=-780,I9100_BCM_CHIP_DISABLED=-781,
 I9100_BCM_CHIP_BUSY=-782,I9100_BCM_CHIP_IO=-783,I9100_BCM_CHIP_ID=-784,
 I9100_BCM_CHIP_LAYOUT=-785,I9100_BCM_CHIP_UNSUPPORTED=-786,I9100_BCM_CHIP_NOT_READY=-787,
 I9100_BCM_CHIP_TIMEOUT=-788};
struct i9100_bcm_core {
 uint32_t base,bytes,wrapper;
 uint16_t id,designer;
 uint8_t revision,reserved[3];
};
struct i9100_bcm_topology {
 uint32_t count,erom_words;
 struct i9100_bcm_core cores[I9100_BCM_MAX_CORES];
};
struct i9100_bcm_chip {
 struct i9100_bcm4330 *bus;struct i9100_bcm_topology topology;
 uintptr_t owner;uint64_t started;
 uint32_t initialized,probed,ram_ready,fault,stage,erom_base,chip_id;
 uint32_t ram_size,retention_size,coreinfo,bank_count,bankinfo[I9100_BCM_MAX_BANKS];
 uint32_t ram_selector_saved,ram_selector_owned;
 uint32_t firmware_holds,ram_image_owned;
 int32_t error,cleanup_error;
};
int i9100_bcm_erom_decode(const uint32_t *,uint32_t word_count,struct i9100_bcm_topology *);
int i9100_bcm_chip_init(struct i9100_bcm_chip *,struct i9100_bcm4330 *);
int i9100_bcm_chip_probe(struct i9100_bcm_chip *,int enabled,int exclusive_board_verified);
int i9100_bcm_chip_ram_probe(struct i9100_bcm_chip *);
int i9100_bcm_chip_ram_restore(struct i9100_bcm_chip *);
int i9100_bcm_chip_close(struct i9100_bcm_chip *);
/* Default OFF, BCM4330 AI/Cortex-M3 only. Reads actual chip ID and bounded
 * DMP EROM, never invents core bases. Complete topology is published only
 * with EOT and unique chipcommon/SDIO/CM3/SOCRAM plus nonaliasing regions.
 * The [18000000,18200000) register envelope, 16 cores and 4MiB RAM are software
 * support limits, not claims that this particular device has those sizes.
 * RAM probing requires CM3 held reset and already-up SOCRAM. It writes only
 * bankidx, preserves/readbacks its original value, and does not reset cores,
 * switch radio clocks, download firmware, remap RAM or start any RF function.
 * A failed selector restore retains ownership/evidence and bus lifetime;
 * explicit restore is the only cleanup, no implicit probe retries.
 * Keep the native F1 host/bus alive and exclusive until both selector and
 * bus window cleanup succeed. A chip hold blocks bus disable/protocol start
 * until explicit close, which refuses unresolved selector/window ownership.
 * Core-down is NOT_READY, not an excuse for automatic reset/power changes.
 * Models/ARM compilation do not prove live topology or firmware execution. */
#ifdef __cplusplus
}
#endif
#endif
