/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_CSIS_H
#define I9100_CSIS_H
#include "i9100_camera_engine_io.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_csis_config {uint32_t width,height,lanes,hs_settle,alignment,external_wclk,data_type;};
struct i9100_csis {
 struct i9100_camera_engine_io io;
 struct i9100_camera_register regs[5];
 struct i9100_csis_config config;
 uintptr_t self;
 uint32_t initialized,enabled,owned,running,generation,raw_status;
 uint32_t errors,non_image_packets,polls;
 int first_error,cleanup_error;
};
int i9100_csis_init(struct i9100_csis *,const struct i9100_camera_engine_io *);
int i9100_csis_enable(struct i9100_csis *,int enabled,int exclusive_power_clock_phy_verified);
int i9100_csis_start(struct i9100_csis *,const struct i9100_csis_config *);
int i9100_csis_poll(struct i9100_csis *);
int i9100_csis_stop(struct i9100_csis *);
int i9100_csis_idle(void *);
/* Exynos4210 CSIS register transport, initially disabled. Its error and
 * non-image-packet interrupts are NOT frame-completion or DMA proof. Only
 * YUV422/8-bit is accepted now; the sensor init/PHY rail/clock owner is
 * independent. Explicit stop disables receiver/local PHY before restoring
 * owned fields. It never changes a shared PMU D-PHY or sensor reset pin. */
#ifdef __cplusplus
}
#endif
#endif
