/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_FIMC_H
#define I9100_FIMC_H
#include "i9100_camera_engine_io.h"
#include "i9100_dma_guard.h"
#include "i9100_video_layout.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_fimc_config {uint32_t width,height,csis_index,input_order422,alignment,invert_vsync,timeout_ms;};
struct i9100_camera_frame {
 uintptr_t self,engine;
 struct i9100_dma_buffer *buffer;
 struct i9100_video_layout layout;
 uint64_t completed_ns;
 uint32_t generation,ticket,bytes,status,status2;
};
struct i9100_fimc {
 struct i9100_camera_engine_io io;
 struct i9100_camera_register regs[24];
 struct i9100_fimc_config config;
 struct i9100_video_layout layout;
 struct i9100_dma_buffer *buffer;
 struct i9100_camera_frame *consumer;
 uintptr_t self;
 uint64_t started,completed_ns;
 uint32_t initialized,enabled,owned,running,held,prepared,ready,generation,ticket,polls;
 uint32_t frame_observed,status,status2;
 int first_error,cleanup_error;
};
int i9100_fimc_init(struct i9100_fimc *,const struct i9100_camera_engine_io *);
int i9100_fimc_enable(struct i9100_fimc *,int enabled,int exclusive_power_clock_sysmmu_verified);
int i9100_fimc_capture(struct i9100_fimc *,const struct i9100_fimc_config *,struct i9100_dma_buffer *);
int i9100_fimc_poll(struct i9100_fimc *);
int i9100_fimc_take_frame(struct i9100_fimc *,struct i9100_camera_frame *);
int i9100_fimc_release_frame(struct i9100_fimc *,struct i9100_camera_frame *);
int i9100_fimc_stop(struct i9100_fimc *);
int i9100_fimc_idle(void *);
/* Exynos4210 FIMC v5.1. One enabled hardware slot, packed YUYV output only,
 * no scaling/rotation/display writeback/sensor reset or arbitrary user DMA.
 * Capture freezes on the first observed fresh FRAMEEND. It may encompass
 * multiple sensor exposures before polling; no exact one-exposure promise.
 * A frame is delivered only after actual capture/scaler/input-DMA idle,
 * fresh LASTCAPTUREEND, stable one-based slot1 completion, address readback
 * and cache ownership; an earlier FRAMEEND alone cannot qualify an image.
 * A borrowed frame pins its allocation and must be explicitly returned;
 * a failed shutdown keeps the device/quarantined allocation and snapshots. */
#ifdef __cplusplus
}
#endif
#endif
