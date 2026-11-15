/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PCM_RING_H
#define I9100_PCM_RING_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_ring_error {I9100_RING_ARGUMENT=-1240,I9100_RING_FULL=-1241,I9100_RING_OWNER=-1242,I9100_RING_OVERFLOW=-1243};
struct i9100_pcm_ring {
 int16_t *storage;uintptr_t self;uint32_t capacity,read_index,write_index,frames;
 uint64_t written,consumed;
};
int i9100_pcm_ring_init(struct i9100_pcm_ring *,int16_t *kernel_storage,uint32_t capacity_frames);
int i9100_pcm_ring_write(struct i9100_pcm_ring *,const int16_t *kernel_samples,uint32_t frames);
int i9100_pcm_ring_peek(struct i9100_pcm_ring *,int16_t *kernel_destination,uint32_t frames);
int i9100_pcm_ring_consume(struct i9100_pcm_ring *,uint32_t frames);
/* Work-loop gate serializes this CPU-only stereo16 staging ring. Atomic
 * append/full rejection, non-destructive peek and commit only after actual
 * period queue success. No DMA alias, user mapping or gain change; the native
 * PCM queue always applies <=-30dB when copying into its owned DMA periods. */
#ifdef __cplusplus
}
#endif
#endif
