/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM4330_H
#define I9100_BCM4330_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_bcm_error {I9100_BCM_ARGUMENT=-720,I9100_BCM_DISABLED=-721,I9100_BCM_BUSY=-722,
 I9100_BCM_IO=-723,I9100_BCM_ID=-724,I9100_BCM_TIMEOUT=-725,I9100_BCM_FRAME=-726,
 I9100_BCM_CREDIT=-727,I9100_BCM_UNSUPPORTED=-728,I9100_BCM_FIRMWARE=-729};
struct i9100_bcm_io {
 void *cookie;
 int (*claim)(void *);void (*release)(void *); /* semantic backplane/packet lock */
 int (*cmd52)(void *,int write,unsigned function,uint32_t address,uint8_t *);
 int (*cmd53)(void *,int write,unsigned function,uint32_t address,int increment,uint8_t *,uint32_t);
 int (*send_frame)(void *,const uint8_t *,uint32_t); /* entire padded F2 packet or error */
 uint64_t (*now_ns)(void *);int (*delay_us)(void *,uint32_t);
};
struct i9100_bcm_frame {
 uint8_t channel,priority;
 const uint8_t *payload;uint32_t bytes;
 uint16_t request_id;uint32_t command,generation,needed_bytes;int32_t status;
};
struct i9100_bcm4330 {
 struct i9100_bcm_io io;
 uint64_t request_started;
 uint32_t initialized,enabled,protocol_ready,fault,chip_id,request_command,request_length;
 uint16_t request_id,next_request;
 int32_t firmware_status;
 uint8_t saved_clock,tx_sequence,tx_max,rx_sequence,rx_seen,flow_mask,pending;
 uint8_t window_saved[3],window_restore_pending,window_touched;
 uint32_t chip_holds;
 void *boot_owner;
 void *control_owner;
 uint32_t protocol_generation,control_generation;
};
int i9100_bcm_init(struct i9100_bcm4330 *,const struct i9100_bcm_io *);
int i9100_bcm_enable(struct i9100_bcm4330 *,int enabled,uint16_t cis_vendor,uint16_t cis_device);
int i9100_bcm_backplane_read(struct i9100_bcm4330 *,uint32_t address,uint32_t *);
int i9100_bcm_protocol_start(struct i9100_bcm4330 *,unsigned negotiated_version,unsigned initial_credit);
int i9100_bcm_send_data(struct i9100_bcm4330 *,const uint8_t *ethernet,unsigned bytes,unsigned priority);
int i9100_bcm_ioctl(struct i9100_bcm4330 *,uint32_t command,int set,const uint8_t *input,unsigned bytes);
int i9100_bcm_receive(struct i9100_bcm4330 *,const uint8_t *frame,unsigned bytes,struct i9100_bcm_frame *);
int i9100_bcm_poll_request(struct i9100_bcm4330 *);
int i9100_bcm_control_bind(struct i9100_bcm4330 *,void *owner,uint32_t firmware_generation);
int i9100_bcm_control_check(struct i9100_bcm4330 *,void *owner,uint32_t firmware_generation);
int i9100_bcm_control_unbind(struct i9100_bcm4330 *,void *owner,int cancel_faulted_request);
int i9100_bcm_ioctl_owned(struct i9100_bcm4330 *,void *owner,uint32_t command,int set,const uint8_t *,unsigned bytes,uint16_t *submitted_id);
int i9100_bcm_send_data_owned(struct i9100_bcm4330 *,void *owner,const uint8_t *,unsigned,unsigned);
/* BCM4330 only, read-only backplane diagnostics and nonglom SDPCM v4/BDC v2.
 * These are transport primitives, not a ready WiFi/iOS network interface.
 * The separate chip/F2 modules supply bounded topology/RAM diagnostics and
 * actual frames; firmware identity/loading/calibration remain prerequisites
 * to protocol_start. No NVRAM country/MAC synthesis, radio
 * reset, image download or implicit retransmission is provided here.
 * One primary control owner per protocol generation; unbinding cannot reuse
 * that generation. Request IDs never wrap. Successful replies must fit the
 * requested capacity and packet; error len is needed_bytes, never payload.
 * A F2 sender must consume synchronously and erase its own secret copies. */
#ifdef __cplusplus
}
#endif
#endif
