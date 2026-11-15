/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_VIDEO_LAYOUT_H
#define I9100_VIDEO_LAYOUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_video_format {I9100_VIDEO_NV12=1,I9100_VIDEO_NV12MT=2,I9100_VIDEO_YUYV=3};
struct i9100_video_layout {uint32_t width,height,format,pitch,plane_count,plane_offset[2],plane_bytes[2],total_bytes;};
int i9100_video_layout(enum i9100_video_format,uint32_t width,uint32_t height,struct i9100_video_layout *);
int i9100_video_range(const struct i9100_video_layout *,uint32_t plane,uint32_t offset,uint32_t bytes,uint32_t allocation_bytes);
/* Geometry, not a tiled image converter or proof of MFC/FIMC output. NV12MT
 * uses 64x32 tile groups,pitch128/luma height32,8KiB plane alignment; tiled
 * pixel addressing/color-space conversion remain a separate adapter. */
#ifdef __cplusplus
}
#endif
#endif
