/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_DMA_NATIVE_H
#define I9100_DMA_NATIVE_H
#include "i9100_dma_guard.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_dma_native;
int i9100_dma_native_allocate(int enabled,uint32_t bytes,void *engine_cookie,int (*engine_idle)(void *),struct i9100_dma_native **);
int i9100_dma_native_allocate_aligned(int enabled,uint32_t bytes,uint32_t alignment,void *engine_cookie,int (*engine_idle)(void *),struct i9100_dma_native **);
struct i9100_dma_buffer *i9100_dma_native_buffer(struct i9100_dma_native *);
int i9100_dma_native_destroy(struct i9100_dma_native **);
/* Real IOKit physically contiguous, wired DMA32 allocation. Every retained
 * CPU alias must be Normal-NC; page-table identity/attributes are checked
 * before the buffer is returned. Allocation never starts an engine.
 * Engine/cookie must remain live until successful destroy. A DEVICE or
 * QUARANTINED buffer cannot be freed, even during a driver's stop failure.
 * Aligned variant accepts page powers of two through128KiB for MFCv5.1;
 * actual CPU alias AND contiguous physical base must satisfy that alignment. */
#ifdef __cplusplus
}
#endif
#endif
