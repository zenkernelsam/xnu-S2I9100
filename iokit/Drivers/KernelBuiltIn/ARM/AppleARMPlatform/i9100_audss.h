/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_AUDSS_H
#define I9100_AUDSS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_audss_error {I9100_AUDSS_ARGUMENT=-440,I9100_AUDSS_DISABLED=-441,
 I9100_AUDSS_BUSY=-442,I9100_AUDSS_IO=-443,I9100_AUDSS_CLOCK=-444};
struct i9100_audss_io {
 void *cookie;
 uint32_t (*read32)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);
 int (*domain_get)(void *);int (*domain_put)(void *);
 int (*consumers_idle)(void *); /* includes I2S and DMA; zero=quiescent */
};
struct i9100_audss_rates {uint32_t xtal,epll,external_i2s,sclk_audio0,bus,i2s;};
struct i9100_audss_config {uint8_t main_source,i2s_source,rp_div,bus_div,i2s_div;};
struct i9100_audss {
 struct i9100_audss_io io;struct i9100_audss_rates rates;
 uint32_t saved[3],enabled,active,initialized,domain_owned,reconfigured,fault;
};
int i9100_audss_init(struct i9100_audss *,const struct i9100_audss_io *,const struct i9100_audss_rates *);
int i9100_audss_enable(struct i9100_audss *,int);
int i9100_audss_acquire(struct i9100_audss *,const struct i9100_audss_config *); /* NULL=adopt clocks */
int i9100_audss_release(struct i9100_audss *);
/* The provider supplies a verified power-domain lease and input rates; no
 * guessed PMU register or EPLL programming. Reconfiguration requires all
 * AUDSS gates initially off. Gate-only adoption preserves inherited clocks.
 * Initialization and enable are software-only. No output pin is enabled. */
#ifdef __cplusplus
}
#endif
#endif
