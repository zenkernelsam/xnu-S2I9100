/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_H
#define I9100_MFC_H
#include "i9100_dma_guard.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_mfc_error {I9100_MFC_ARGUMENT=-840,I9100_MFC_DISABLED=-841,I9100_MFC_BUSY=-842,
 I9100_MFC_IO=-843,I9100_MFC_TIMEOUT=-844,I9100_MFC_FIRMWARE=-845,I9100_MFC_REPLY=-846};
struct i9100_mfc_io {
 void *cookie;uint32_t (*read32)(void *,unsigned);void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);uint64_t (*now_ns)(void *);
 int (*delay_us)(void *,uint32_t);void (*barrier)(void *);
};
struct i9100_mfc {
 struct i9100_mfc_io io;
 struct i9100_dma_buffer *bank_a,*bank_b;
 uint64_t started;
 uint32_t initialized,enabled,risc_running,fault,state,pending,expected,firmware_bytes,firmware_version;
 uint32_t instance,instance_open,result[4],last_reply;
};
int i9100_mfc_init(struct i9100_mfc *,const struct i9100_mfc_io *);
int i9100_mfc_enable(struct i9100_mfc *,int enabled,int exclusive_power_clock_identity_sysmmu_verified);
int i9100_mfc_boot(struct i9100_mfc *,struct i9100_dma_buffer *bank_a,struct i9100_dma_buffer *bank_b,uint32_t verified_firmware_bytes);
int i9100_mfc_sys_init(struct i9100_mfc *);
int i9100_mfc_open_h264(struct i9100_mfc *,uint32_t context_offset,uint32_t context_bytes);
int i9100_mfc_close(struct i9100_mfc *);
int i9100_mfc_poll(struct i9100_mfc *);
int i9100_mfc_stop(struct i9100_mfc *);
int i9100_mfc_idle(void *);
/* Exynos4210 MFCv5 only. Two private,8KiB-aligned DMA32 arenas; firmware
 * already authenticated/copied into the beginning of bankA by a provider.
 * Baselines aren't inferred from RISC_ON/-1 v6 definitions. Arenas remain
 * device-owned until RISC reset + MC idle are both proved. No decode/encode
 * frame submission until DPB/tiled formats/SYSMMU mappings are implemented.
 * Boot/open/close are true mailbox operations, not successful media stubs. */
#ifdef __cplusplus
}
#endif
#endif
