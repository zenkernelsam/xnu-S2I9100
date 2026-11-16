/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_F2_H
#define I9100_BCM_F2_H
#include "i9100_sdio.h"
#include "i9100_bcm4330.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_F2_FRAME_MAX 2048U
enum i9100_f2_error {I9100_F2_ARGUMENT=-1040,I9100_F2_DISABLED=-1041,I9100_F2_BUSY=-1042,
 I9100_F2_IO=-1043,I9100_F2_FRAME=-1044,I9100_F2_GENERATION=-1045,I9100_F2_UNSUPPORTED=-1046};
struct i9100_f2_io {
 void *cookie;int (*lock)(void *);void (*unlock)(void *);
 uint64_t (*now_ns)(void *);int (*delay_us)(void *,uint32_t);
};
struct i9100_bcm_f2 {
 struct i9100_f2_io io;struct i9100_sdio *card;
 uint32_t initialized,enabled,fault,generation,tx_frames,rx_frames,empty_reads;
 uint32_t stage,last_error,last_transfer,logical_bytes,wire_bytes;
 uint32_t cleanup_f1;
 uint8_t last_header[32],scratch[2080]; /* 32B header + rounded2048B remainder */
};
int i9100_bcm_f2_init(struct i9100_bcm_f2 *,struct i9100_sdio *,const struct i9100_f2_io *);
int i9100_bcm_f2_enable(struct i9100_bcm_f2 *,int enabled,uint32_t verified_firmware_link_generation);
int i9100_bcm_f2_get_io(struct i9100_bcm_f2 *,struct i9100_bcm_io *);
int i9100_bcm_f2_receive(struct i9100_bcm_f2 *,uint8_t *frame,unsigned capacity,unsigned *bytes);
/* Real CMD52/CMD53 adapter for F1 probe/backplane and F2 packets. Owns a
 * semantic lock distinct from the host-controller lock; card/context outlive
 * it and the caller serializes any returned frame views/BCM decode calls.
 * Disabled by default; F2 ready+512-byte FBR blocksize are checked against
 * the device before first IO. enable never enables/downloads firmware.
 * Header32/4-byte alignment and block-rounded F2SYNC@8000 fixed FIFO match
 * pinned bcmdhd; malformed/partial packets quarantine, with no silent drain,
 * replay, auto-NAK or reset. A new actual firmware/FIFO generation is needed
 * before re-enable. All-zero len/check means no frame, not valid data. */
#ifdef __cplusplus
}
#endif
#endif
