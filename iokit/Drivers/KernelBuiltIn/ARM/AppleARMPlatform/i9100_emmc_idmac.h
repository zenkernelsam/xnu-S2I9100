/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_IDMAC_H
#define I9100_EMMC_IDMAC_H
#include "i9100_emmc.h"
#include "i9100_dma_guard.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_IDMAC_DESC_BYTES 4096U
#define I9100_IDMAC_DATA_BYTES (512U*I9100_EMMC_MAX_SECTORS)
#define I9100_IDMAC_CHUNK 4096U
struct i9100_emmc_idmac_binding {
 uintptr_t self,card;uint32_t generation,cid[4],enabled,active,poisoned;
 uint32_t command,argument,sectors,writing,ciu_bytes,host_bytes,command_submitted;
 void *cookie;
 int (*begin)(void *,struct i9100_emmc_card *,unsigned,uint32_t,uint32_t,int,const uint8_t *);
 int (*finish)(void *,struct i9100_emmc_card *,uint8_t *);
 int (*fail)(void *,struct i9100_emmc_card *,int);
};
enum i9100_emmc_idmac_phase {I9100_IDMAC_NEW=0,I9100_IDMAC_BOUND=1,I9100_IDMAC_PREPARED=2,
 I9100_IDMAC_ARMED=3,I9100_IDMAC_COMPLETING=4,I9100_IDMAC_FAILED=5,I9100_IDMAC_CLOSED=6};
struct i9100_emmc_idmac_io {
 void *cookie;int (*board_check)(void *);void (*barrier)(void *);
 uint32_t descriptors_normal_nc; /* Supplied by verified native allocator/aliases. */
};
struct i9100_emmc_idmac {
 struct i9100_emmc_idmac_binding binding;
 struct i9100_emmc_idmac_io io;
 struct i9100_dma_buffer *descriptors,*data;
 uint32_t phase,attempts,completed,failure_phase,descriptors_used,bytes;
 uint32_t saved_ctrl,saved_bmod,saved_fifoth,saved_dbaddr;
 uint32_t last_rint,last_idsts,last_status,mode_started,mode_stopped,mutation_possible,buffer_holds;
 int error,cleanup_error;
};
int i9100_emmc_idmac_bind(int enabled,struct i9100_emmc_idmac *,struct i9100_emmc_card *,
 const struct i9100_emmc_idmac_io *,struct i9100_dma_buffer *descriptors,struct i9100_dma_buffer *data);
int i9100_emmc_idmac_unbind(struct i9100_emmc_idmac *,struct i9100_emmc_card *);
int i9100_emmc_idmac_buffer_idle(struct i9100_emmc_idmac *,struct i9100_emmc_card *);
/* Only the MMC core issues its existing checked CMD17/18 or granted CMD24/25.
 * Internal IDMAC32 is optional and off by default. Four16B descriptors at
 * most, owned Normal-NC contiguous DMA buffers, exact length/next/PA checks.
 * Completion needs actual DTO, direction-complete/normal IDSTS, both byte
 * counters, descriptor ownership return and actual DMA quiescence before
 * buffers become CPU-owned. Card DAT busy is separately drained by the MMC
 * CMD12/TRAN path; DMA idle alone is not permission to free a card owner.
 * Errors preserve original cause, quarantine handed buffers, retain native
 * maps/lease; no reset, retry, automatic PIO fallback or extra command API. */
#ifdef __cplusplus
}
#endif
#endif
