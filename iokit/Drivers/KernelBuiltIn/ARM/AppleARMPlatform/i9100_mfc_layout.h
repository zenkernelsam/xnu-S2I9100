/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_LAYOUT_H
#define I9100_MFC_LAYOUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_MFC_BASE_ALIGNMENT 131072U
#define I9100_MFC_H264_CONTEXT 614400U
#define I9100_MFC_CPB_BYTES 4194304U
#define I9100_MFC_DESC_BYTES 131072U
struct i9100_mfc_h264_plan {
 uint32_t system_bytes,context_offset,context_bytes,shared_offset,shared_bytes;
 uint32_t descriptor_offset,descriptor_bytes,stream_offset,stream_capacity,stream_bytes,end_offset;
};
struct i9100_mfc_h264_sizes {
 uint32_t width,height,pitch,aligned_height,dpb_count,luma_bytes,chroma_bytes,mv_bytes;
 uint32_t bank_a_scratch,bank_a_dpb_bytes,bank_b_dpb_bytes;
};
int i9100_mfc_h264_plan(uint32_t system_bytes,uint32_t bank_a_bytes,uint32_t stream_bytes,struct i9100_mfc_h264_plan *);
int i9100_mfc_h264_sizes(uint32_t width,uint32_t height,uint32_t dpb_count,struct i9100_mfc_h264_sizes *);
/* Register/source-derived v5.1 layout only, not a bitstream parser or tiled
 * converter. Context is 600KiB, descriptors128KiB, shared4KiB, CPB4MiB.
 * Luma/MV are bankB-relative; chroma and neighbor scratch are bankA-relative.
 * Host shared-memory address uses bytes, unlike 2KiB register addresses. */
#ifdef __cplusplus
}
#endif
#endif
