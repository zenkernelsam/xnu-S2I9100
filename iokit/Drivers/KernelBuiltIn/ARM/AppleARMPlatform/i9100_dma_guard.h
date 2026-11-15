/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_DMA_GUARD_H
#define I9100_DMA_GUARD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_dma_state {I9100_DMA_UNBOUND=0,I9100_DMA_BOUND=1,I9100_DMA_CPU=2,I9100_DMA_DEVICE=3,I9100_DMA_QUARANTINED=4,I9100_DMA_RELEASED=5};
enum i9100_dma_direction {I9100_DMA_TO_DEVICE=1,I9100_DMA_FROM_DEVICE=2,I9100_DMA_BIDIRECTIONAL=3};
enum i9100_dma_sync {I9100_DMA_CLEAN=1,I9100_DMA_CLEAN_INVALIDATE=2,I9100_DMA_INVALIDATE=3,I9100_DMA_BARRIER=4};
enum i9100_dma_error {I9100_DMA_ARGUMENT=-360,I9100_DMA_DISABLED=-361,I9100_DMA_BUSY=-362,I9100_DMA_OWNER=-363,I9100_DMA_IO=-364};
struct i9100_dma_io {
 void *cookie;
 int (*lock)(void *);void (*unlock)(void *);
 int (*sync)(void *,uintptr_t cpu,uint32_t physical,uint32_t bytes,unsigned action);
 int (*is_idle)(void *); /* zero only after actual controller/channel quiescence */
 void (*release)(void *,uintptr_t cpu,uint32_t physical,uint32_t bytes);
};
struct i9100_dma_buffer {struct i9100_dma_io io;uintptr_t cpu,self;uint32_t physical,bytes,state,direction,handed_to_device;};
int i9100_dma_bind(struct i9100_dma_buffer *,const struct i9100_dma_io *,uintptr_t,uint32_t,uint32_t);
int i9100_dma_enable(struct i9100_dma_buffer *,int);
int i9100_dma_prepare(struct i9100_dma_buffer *,enum i9100_dma_direction);
int i9100_dma_complete(struct i9100_dma_buffer *);
int i9100_dma_abort(struct i9100_dma_buffer *);
int i9100_dma_release(struct i9100_dma_buffer *);
/* This owned-buffer/cache lifecycle does NOT submit PL330/IDMAC jobs. The
 * future engine adapter proves idle and provides range-only cache operations.
 * CPU/physical allocation owns whole64B lines; no arbitrary user addresses. */
#ifdef __cplusplus
}
#endif
#endif
