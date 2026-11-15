/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_CAMERA_PROTOCOL_H
#define I9100_CAMERA_PROTOCOL_H
#include "i9100_peripheral_io.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_camera_kind {I9100_CAMERA_M5MO=1,I9100_CAMERA_S5K5BAFX=2};
enum i9100_camera_error {I9100_CAM_ARGUMENT=-800,I9100_CAM_DISABLED=-801,I9100_CAM_BUSY=-802,
 I9100_CAM_IO=-803,I9100_CAM_ID=-804,I9100_CAM_FRAME=-805,I9100_CAM_TIMEOUT=-806};
struct i9100_camera {
 struct i9100_peripheral_i2c io;
 uint32_t initialized,enabled,fault,kind,project,firmware,hardware;
};
int i9100_camera_init(struct i9100_camera *,const struct i9100_peripheral_i2c *,enum i9100_camera_kind);
int i9100_camera_enable(struct i9100_camera *,int enabled,int rails_clock_reset_verified);
int i9100_m5mo_read(struct i9100_camera *,uint8_t category,uint8_t reg,unsigned width,uint32_t *);
int i9100_m5mo_set_parameter_mode(struct i9100_camera *);
int i9100_s5k5bafx_read(struct i9100_camera *,uint16_t page,uint16_t address,uint16_t *);
/* Protocol/identity diagnostics only. M5MO's read command and response have
 * separate STOP-delimited transactions; responses must have an exact length.
 * S5K5BAFX uses 002C/002E/0F12 page/address/window, not M5MO registers.
 * No firmware flash/memory writes, autofocus, sensor-init table or streaming
 * API; CSI/FIMC and image ownership are separate required stages. */
#ifdef __cplusplus
}
#endif
#endif
