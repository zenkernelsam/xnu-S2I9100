/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_I2S_H
#define I9100_I2S_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_i2s_error {I9100_I2S_ARGUMENT=-600,I9100_I2S_DISABLED=-601,
 I9100_I2S_BUSY=-602,I9100_I2S_IO=-603,I9100_I2S_CLOCK=-604};
struct i9100_i2s_io {
 void *cookie;uint32_t (*read32)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);
 int (*dma_idle)(void *);int (*dma_stop)(void *);
};
struct i9100_i2s {
 struct i9100_i2s_io io;
 uint32_t initialized,enabled,prepared,active,fault,saved[3],sample_rate;
};
int i9100_i2s_init(struct i9100_i2s *,const struct i9100_i2s_io *);
int i9100_i2s_enable(struct i9100_i2s *,int enabled);
int i9100_i2s_configure(struct i9100_i2s *,uint32_t sample_rate,uint32_t measured_bclk,int gpio_clock_lease_verified);
int i9100_i2s_start(struct i9100_i2s *,int capture,int muted);
int i9100_i2s_quiesce(struct i9100_i2s *); /* suppress and verify requests, retain configuration/lease */
int i9100_i2s_stop(struct i9100_i2s *);
void i9100_pcm_attenuate_30db(int16_t *destination,const int16_t *source,unsigned samples);
/* I2S0 main FIFO, codec-master/slave-AP, stereo16/BCLK32fs. Configure while
 * idle; no secondary/IDMA/FIFO sharing or arbitrary clock inference.
 * Start requires mute acknowledged. Stop suppresses requests before killing
 * DMA, and only restores registers after the engine is quiescent. */
#ifdef __cplusplus
}
#endif
#endif
