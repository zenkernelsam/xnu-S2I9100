/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SDIO_H
#define I9100_SDIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_sdio_error {I9100_SDIO_ARGUMENT=-680,I9100_SDIO_DISABLED=-681,I9100_SDIO_BUSY=-682,
 I9100_SDIO_IO=-683,I9100_SDIO_TIMEOUT=-684,I9100_SDIO_CRC=-685,I9100_SDIO_CARD=-686};
struct i9100_sdio_io {
 void *cookie;uint8_t (*read8)(void *,unsigned);uint16_t (*read16)(void *,unsigned);
 uint32_t (*read32)(void *,unsigned);
 void (*write8)(void *,unsigned,uint8_t);void (*write16)(void *,unsigned,uint16_t);
 void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);
 uint64_t (*now_ns)(void *);int (*delay_us)(void *,uint32_t);
};
struct i9100_sdio {
 struct i9100_sdio_io io;
 uint64_t started,timeout_ns;
 uint32_t initialized,active,fault,input_clock_hz,clock_hz,ocr,rca,functions,last_status,last_response;
 uint32_t poll_limit,saved_inten,saved_signal;
 uint32_t saved_feedback[2],host_owned;
 uint16_t block_size[3],vendor,device;
 uint8_t revision,saved_io_enable,capabilities,bus_control,last_function;
};
int i9100_sdio_init(struct i9100_sdio *,const struct i9100_sdio_io *,uint32_t verified_input_clock_hz);
int i9100_sdio_enable(struct i9100_sdio *,int enabled,int board_lease_verified);
int i9100_sdio_release_host(struct i9100_sdio *); /* cleanup even after a failed initial probe */
int i9100_sdio_function(struct i9100_sdio *,unsigned function,int enabled);
int i9100_sdio_set_bus(struct i9100_sdio *,int four_bit,uint32_t target_clock_hz);
int i9100_sdio_cmd52(struct i9100_sdio *,int write,unsigned function,uint32_t address,uint8_t *value);
int i9100_sdio_cmd53(struct i9100_sdio *,int write,unsigned function,uint32_t address,int increment,uint8_t *bytes,uint32_t length);
/* Independent SDHCI3@12540000, polled PIO; not a storage/SD-memory driver.
 * CMD0/5/3/7 then CCCR/FBR/CIS. Input clock, Samsung feedback path, GPIO and
 * BCM power are owned/verified by the provider. Starts <=400kHz/1bit; no UHS,
 * guessed tuning, sector writes or concurrent external-SD controller use.
 * Exposes functions1/2 only. Failed transfers quarantine instead of retry. */
#ifdef __cplusplus
}
#endif
#endif
