/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_FIRMWARE_H
#define I9100_BCM_FIRMWARE_H
#include "i9100_bcm_chip.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_BCM_NVRAM_MAX 4096U
enum i9100_bcm_fw_error {I9100_BCM_FW_ARGUMENT=-800,I9100_BCM_FW_DISABLED=-801,
 I9100_BCM_FW_BUSY=-802,I9100_BCM_FW_IO=-803,I9100_BCM_FW_LAYOUT=-804,
 I9100_BCM_FW_ID=-805,I9100_BCM_FW_CRYPTO=-806,I9100_BCM_FW_HASH=-807,
 I9100_BCM_FW_VERIFY=-808,I9100_BCM_FW_NOT_READY=-809,I9100_BCM_FW_TIMEOUT=-810};
struct i9100_bcm_fw_layout {
 uint32_t ram_bytes,firmware_bytes,firmware_padded,nvram_bytes,nvram_padded,nvram_address;
 uint32_t token_address,token;
};
struct i9100_bcm_fw_profile {
 uint32_t chip_revision,ram_bytes,firmware_bytes,nvram_bytes;
 uint8_t firmware_sha256[32],nvram_sha256[32],verified_factory_mac[6];
 uint16_t board_data_reviewed;
};
struct i9100_bcm_fw_integrity {
 void *cookie;int (*sha256)(void *,const void *,uint32_t bytes,uint8_t digest[32]);
};
struct i9100_bcm_firmware {
 struct i9100_bcm_chip *chip;struct i9100_bcm_fw_profile profile;
 struct i9100_bcm_fw_layout layout;struct i9100_bcm_fw_integrity integrity;
 uintptr_t owner;uint64_t started;
 uint32_t initialized,enabled,loaded,fault,stage,confirmed_firmware_bytes,confirmed_nvram_bytes;
 uint32_t transfer_address,transfer_bytes,token_verified;
 uint32_t boot_holds;
 uint32_t execution_possible;
 int32_t error,cleanup_error;
};
int i9100_bcm_fw_make_layout(uint32_t ram_bytes,uint32_t sdio_core_revision,uint32_t firmware_bytes,uint32_t nvram_bytes,struct i9100_bcm_fw_layout *);
int i9100_bcm_nvram_validate(const uint8_t *,uint32_t bytes,const uint8_t verified_factory_mac[6]);
int i9100_bcm_fw_init(struct i9100_bcm_firmware *,int enabled,struct i9100_bcm_chip *,const struct i9100_bcm_fw_profile *,const struct i9100_bcm_fw_integrity *);
int i9100_bcm_fw_load(struct i9100_bcm_firmware *,const uint8_t *immutable_kernel_image,const uint8_t *immutable_packed_nvram);
int i9100_bcm_fw_abort(struct i9100_bcm_firmware *);
int i9100_bcm_fw_close(struct i9100_bcm_firmware *);
int i9100_bcm_fw_kernel_sha256(void *,const void *,uint32_t,uint8_t digest[32]);
/* Default OFF, RAM staging only, no ARM reset release or RF activation.
 * CM3 and the unique D11 core must both already be held reset, so a running
 * D11 bus master cannot mutate SRAM during readback. SOCRAM must already be up.
 * Exact chip revision/actual RAM, approved image/NVRAM SHA256 and separately
 * sourced factory MAC are mandatory. No invented calibration/country/MAC.
 * Packed NVRAM is bounded single-board key=value records with final double
 * NUL. Caller owns/pins both kernel buffers and keeps them immutable for the
 * entire synchronous operation; no raw user pointers or arbitrary opcodes.
 * Clear/readback token first, copy zero-padded <=512B chunks across32KiB
 * windows, readback each chunk, recheck hashes, write/check token last.
 * The rev7 SDIO PR85623 four-byte padding is part of the layout.
 * A failure after any attempted RAM write retains ram_image_owned and the
 * chip hold. No retry or pretending firmware ran. Explicit abort, while CM3
 * and D11 remain reset and SOCRAM is up, clears/readbacks token before releasing
 * ownership. It does not restore the previous RAM image. Failed abort/close
 * retains the context for board-level reset/recovery by its owner.
 * loaded means staged bytes/readbacks/hash/token verified; the bootstrap
 * execution_possible flag distinguishes later CPU release from halted RAM.
 * Boot/handshake/firmware provenance/calibration/network/iOS
 * service are separate required stages. Kernel SHA uses registered crypto
 * only, rejects missing/invalid provider and never fakes a valid digest. */
#ifdef __cplusplus
}
#endif
#endif
