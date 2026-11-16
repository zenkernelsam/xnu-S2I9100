/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_BOOT_H
#define I9100_BCM_BOOT_H
#include "i9100_bcm_firmware.h"
#include "i9100_bcm_f2.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_bcm_boot_error {I9100_BCM_BOOT_ARGUMENT=-900,I9100_BCM_BOOT_DISABLED=-901,
 I9100_BCM_BOOT_BUSY=-902,I9100_BCM_BOOT_IO=-903,I9100_BCM_BOOT_NOT_READY=-904,
 I9100_BCM_BOOT_TIMEOUT=-905,I9100_BCM_BOOT_PROTOCOL=-906,I9100_BCM_BOOT_HALTED=-907};
struct i9100_bcm_boot {
 struct i9100_bcm_firmware *firmware;struct i9100_bcm_f2 *packets;uintptr_t self;
 uint64_t started;
 uint32_t initialized,enabled,ready,running,fault,stage,generation,sdio_base;
 uint32_t owned,clock_owned,fbr_owned,version_owned,io_enable_owned;
 uint32_t saved_version,saved_block_size,mailbox,interrupt_status,attempted,stopped;
 uint8_t saved_clock,saved_enable,saved_fbr[2];
 int32_t error,cleanup_error;
};
int i9100_bcm_boot_init(struct i9100_bcm_boot *,int enabled,struct i9100_bcm_firmware *,struct i9100_bcm_f2 *,uint32_t fresh_firmware_generation);
int i9100_bcm_boot_start(struct i9100_bcm_boot *);
int i9100_bcm_boot_stop(struct i9100_bcm_boot *);
int i9100_bcm_boot_close(struct i9100_bcm_boot *);
/* Default OFF, requires approved SHA/MAC-pinned halted RAM staging and native
 * board/host ownership. Start requests/readbacks HT, sets host version/F2
 * block size, clears stale HMB bits while cores held reset, then releases
 * CM3 only. A NEW HMB_SW1 event + FWREADY/DEVREADY + version4 and IOR2 are
 * required; cached mailbox bytes are not handshake evidence. No IRQ enable.
 * Vendor attach seed tx_seq255/tx_max0 grants one initial control-only slot;
 * data remains blocked until real received frames supply further credits.
 * Firmware execution may perform RF work; this is not an RF-off promise or
 * authorization for real hardware. There is no flash/NV write or scan API.
 * FWHALT/version mismatch/time/IO failure never auto-restarts. Keep all owners
 * until explicit stop: deny protocol traffic, halt CM3/D11, disable F2,
 * invalidate RAM token, restore private clock/FBR/version. Stop's F1 cleanup
 * mode may bypass packet quarantine only with an alive nonfaulted card; a
 * host/card fault still blocks access and needs board recovery. Failed stop
 * retains context/FW hold. Close is allowed only after complete stop or an
 * untouched session; the owner then closes FW/core/chip/native/radio in order.
 * Polling handshake is not Wi-Fi/iOS networking or actual firmware proof
 * when obtained from a model. Borrower lifetime must be serialized by owner. */
#ifdef __cplusplus
}
#endif
#endif
