/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PL330_H
#define I9100_PL330_H
#include "i9100_dma_guard.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_pl330_mode {I9100_PL330_MEMORY=1,I9100_PL330_I2S_TX=2,I9100_PL330_I2S_RX=3};
enum i9100_pl330_error {I9100_PL330_ARGUMENT=-560,I9100_PL330_DISABLED=-561,
 I9100_PL330_BUSY=-562,I9100_PL330_ID=-563,I9100_PL330_IO=-564,
 I9100_PL330_TIMEOUT=-565,I9100_PL330_FAULT=-566};
struct i9100_pl330_io {
 void *cookie;
 uint32_t (*read32)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);
 uint64_t (*now_ns)(void *);int (*delay_us)(void *,uint32_t);
};
struct i9100_pl330_job {
 enum i9100_pl330_mode mode;
 struct i9100_dma_buffer *code,*source,*destination;
 uint32_t source_offset,destination_offset,bytes,timeout_ms;
};
struct i9100_pl330 {
 struct i9100_pl330_io io;
 struct i9100_pl330_job job;
 uint64_t started;
 uint32_t initialized,enabled,active,fault,channel,event,saved_inten,channels,events,peripherals;
 uint32_t program_bytes,completed,error_status;
};
int i9100_pl330_init(struct i9100_pl330 *,const struct i9100_pl330_io *,unsigned channel,unsigned event);
int i9100_pl330_enable(struct i9100_pl330 *,int);
int i9100_pl330_encode(enum i9100_pl330_mode,uint32_t source,uint32_t destination,uint32_t bytes,unsigned event,uint8_t *program,unsigned capacity,unsigned *used);
int i9100_pl330_submit(struct i9100_pl330 *,const struct i9100_pl330_job *);
int i9100_pl330_poll(struct i9100_pl330 *,uint32_t *bytes_completed);
int i9100_pl330_stop(struct i9100_pl330 *);
int i9100_pl330_idle(void *); /* DMA guard callback, reads only; no recursive engine lock */
/* Provider exclusively claims this PDMA0 channel/event and request11/12 for
 * I2S RX/TX; controller debug mutex is shared across all channels. No global
 * reset, arbitrary firmware microcode, user addresses or indefinite cycles.
 * I2S submit/poll errors retain the active job: the I2S owner must quiesce
 * requests before explicit stop. Memory jobs may abort immediately.
 * A failed stop retains references to/quarantines all handed buffers. */
#ifdef __cplusplus
}
#endif
#endif
