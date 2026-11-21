/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_H
#define I9100_MFC_H
#include "i9100_dma_guard.h"
#include "i9100_mfc_layout.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_mfc_error {I9100_MFC_ARGUMENT=-840,I9100_MFC_DISABLED=-841,I9100_MFC_BUSY=-842,
 I9100_MFC_IO=-843,I9100_MFC_TIMEOUT=-844,I9100_MFC_FIRMWARE=-845,I9100_MFC_REPLY=-846,
 I9100_MFC_OWNER=-847,I9100_MFC_HASH=-848,I9100_MFC_LAYOUT=-849};
struct i9100_mfc_firmware_profile {
 uint32_t board_data_reviewed,system_bytes,firmware_bytes,expected_version,timeout_ms;
 uint8_t sha256[32];
};
struct i9100_mfc_io {
 void *cookie;uint32_t (*read32)(void *,unsigned);void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);uint64_t (*now_ns)(void *);
 int (*delay_us)(void *,uint32_t);void (*barrier)(void *);
 int (*sha256)(void *,const void *,uint32_t,uint8_t[32]);
};
struct i9100_mfc_decoder;
struct i9100_mfc {
 struct i9100_mfc_io io;
 struct i9100_dma_buffer *bank_a,*bank_b;
 uint64_t started;
 uint32_t initialized,enabled,risc_running,fault,state,pending,expected,firmware_bytes,firmware_version;
 uint32_t instance,instance_open,result[4],last_reply;
 uintptr_t self;
 struct i9100_mfc_firmware_profile profile;
 struct i9100_mfc_h264_plan plan;
 struct i9100_mfc_h264_sizes sequence;
 uint32_t staged,h264_staged,boot_attempted,held_mask,prepared_mask,generation,polls;
 uint32_t initial_bank_a,initial_bank_b,initial_channel[2],sys_memory_bytes,consumed_bytes,raw_interrupt;
 uint32_t banks_written,channels_written,last_channel0;
 uint8_t stream_sha256[32];
 int first_error,cleanup_error;
 struct i9100_mfc_decoder *decoder;
};
int i9100_mfc_init(struct i9100_mfc *,const struct i9100_mfc_io *);
int i9100_mfc_enable(struct i9100_mfc *,int enabled,int exclusive_power_clock_identity_sysmmu_verified);
int i9100_mfc_stage_firmware(struct i9100_mfc *,struct i9100_dma_buffer *bank_a,struct i9100_dma_buffer *bank_b,
                           const struct i9100_mfc_firmware_profile *,const void *firmware,uint32_t bytes);
int i9100_mfc_stage_h264(struct i9100_mfc *,const void *annexb_stream,uint32_t bytes);
int i9100_mfc_boot(struct i9100_mfc *);
int i9100_mfc_sys_init(struct i9100_mfc *);
int i9100_mfc_open_h264(struct i9100_mfc *);
int i9100_mfc_parse_h264_header(struct i9100_mfc *);
int i9100_mfc_close(struct i9100_mfc *);
int i9100_mfc_poll(struct i9100_mfc *);
int i9100_mfc_stop(struct i9100_mfc *);
int i9100_mfc_idle(void *);
/* Internal decoder/core boundary; caller must already hold engine lock. */
int i9100_mfc_fail_locked(struct i9100_mfc *,int);
/* Exynos4210 MFCv5.1 only. Two private,128KiB-aligned DMA32 arenas; firmware
 * source and landed bytes both SHA-authenticated before RISC launch.
 * Explicit reviewed profile chooses 384/512KiB system reservation, NOT file
 * length. H264 context600KiB/descriptor/CPB/shared use the validated plan.
 * Baselines aren't inferred from RISC_ON/-1 v6 definitions. Arenas remain
 * device-owned until RISC reset + MC idle are both proved. No decode/encode
 * frame submission without the separate checked DPB/lease/tiled adapter.
 * Boot/sysinit/open/header/close are true firmware operations, not media
 * stubs. Poll deadline applies even if a late reply is present; raw response
 * and cleanup error are kept. Failed stop retains held allocations. All
 * APIs use one caller-owned engine address and thread-context IO locking. */
#ifdef __cplusplus
}
#endif
#endif
