/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_CAMERA_NATIVE_H
#define I9100_CAMERA_NATIVE_H
#include "i9100_csis.h"
#include "i9100_fimc.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_camera_native;
struct i9100_camera_supply_lease {
 uintptr_t self,provider;
 uint32_t owned,generation,csis_index;
};
struct i9100_camera_supply {
 void *cookie;
 int (*acquire)(void *,unsigned csis_index,struct i9100_camera_supply_lease *);
 int (*validate)(void *,const struct i9100_camera_supply_lease *);
 int (*release)(void *,struct i9100_camera_supply_lease *);
};
struct i9100_camera_native_config {
 struct i9100_csis_config csis;
 struct i9100_fimc_config fimc;
};
struct i9100_camera_native_status {
 uint32_t phase,csis_running,fimc_running,frame_ready,supply_generation;
 uint32_t csis_status,csis_errors,fimc_status,fimc_status2,buffer_state,buffer_holds;
 int error,cleanup_error;
};
int i9100_camera_native_open(int enabled,struct i9100_peripheral_native *,const struct i9100_camera_supply *,
                            const struct i9100_camera_native_config *,struct i9100_camera_native **);
int i9100_camera_native_start(struct i9100_camera_native *);
int i9100_camera_native_poll(struct i9100_camera_native *);
int i9100_camera_native_take_frame(struct i9100_camera_native *,struct i9100_camera_frame *);
int i9100_camera_native_release_frame(struct i9100_camera_native *,struct i9100_camera_frame *);
int i9100_camera_native_status(struct i9100_camera_native *,struct i9100_camera_native_status *);
int i9100_camera_native_close(struct i9100_camera_native **);
/* Default OFF; FIMC0 plus one CSIS, real IOKit maps and a private DMA32
 * allocation. A single module reservation prevents two FIMC0 users.
 * The required supply provider owns the sensor, reviewed identity/init
 * table, shared rails/PMU PHY, source clocks, gate manager and teardown.
 * Its acquire/validate/release MUST represent actual owned resources, not
 * constant success or a model's "verified" flag. This provider is not yet
 * implemented by the production platform. No empty provider is published.
 * Failed acquire may retain lease.owned; failed close retains context,
 * frame, maps and supply until explicit safe cleanup. Caller serializes
 * entry against final close; a borrowed frame prevents that close. */
#ifdef __cplusplus
}
#endif
#endif
