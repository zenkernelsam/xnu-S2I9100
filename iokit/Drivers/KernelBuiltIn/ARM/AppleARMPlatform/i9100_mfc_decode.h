/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_DECODE_H
#define I9100_MFC_DECODE_H
#include "i9100_mfc.h"
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_MFC_DPB_MAX 16U
#define I9100_MFC_DPB_REGS (3U*I9100_MFC_DPB_MAX+4U)
struct i9100_mfc_dpb_layout {
 struct i9100_mfc_h264_sizes sizes;
 uint32_t count,scratch_mv,scratch_ip,end_a,end_b;
 uint32_t luma[I9100_MFC_DPB_MAX],chroma[I9100_MFC_DPB_MAX],mv[I9100_MFC_DPB_MAX];
};
struct i9100_mfc_decode_io {
 void *cookie;
 int (*normal_nc)(void *,const struct i9100_dma_buffer *,const struct i9100_dma_buffer *);
};
struct i9100_mfc_frame {
 uint32_t slot,epoch,tag,display_sequence,width,height,pitch,luma_bytes,chroma_bytes;
 uint32_t display_status,decode_status,frame_type,crop_left,crop_right,crop_top,crop_bottom;
};
struct i9100_mfc_frame_lease {
 uintptr_t self,engine;uint32_t generation,owned;
 struct i9100_mfc_frame frame;
};
struct i9100_mfc_decode_reg {uint32_t offset,saved,written;};
struct i9100_mfc_decoder {
 uintptr_t self,engine;struct i9100_mfc_decode_io io;
 struct i9100_mfc_dpb_layout layout;
 struct i9100_mfc_decode_reg regs[I9100_MFC_DPB_REGS];
 struct i9100_mfc_frame frames[I9100_MFC_DPB_MAX];
 struct i9100_mfc_frame_lease *leases[I9100_MFC_DPB_MAX];
 uint32_t enabled,generation,configured,ready,closed,stopping,fault,registers,available,display_ready,leased,draining,drained;
 uint32_t frame_submitted,frame_completed,displayed,released,last_tag,input_bytes,input_start,input_tag,submitted_release;
 uint32_t submitted_bytes,submitted_start,submitted_tag,submitted_last,no_progress,drain_commands;
 uint32_t raw_display,raw_decoded,raw_type,raw_channel,raw_consumed,raw_display_y,raw_display_c,raw_decoded_y,raw_decoded_c;
 uint32_t raw_display_type,raw_tag,raw_crop_h,raw_crop_v;
 uint8_t input_sha256[32];int error,cleanup_error;
};
int i9100_mfc_dpb_layout(const struct i9100_mfc_h264_plan *,const struct i9100_mfc_h264_sizes *,
 uint32_t bank_a_bytes,uint32_t bank_b_bytes,struct i9100_mfc_dpb_layout *);
int i9100_mfc_decoder_bind(int enabled,struct i9100_mfc *,struct i9100_mfc_decoder *,const struct i9100_mfc_decode_io *);
int i9100_mfc_decoder_init_buffers(struct i9100_mfc *);
int i9100_mfc_decoder_frame(struct i9100_mfc *,const void *annexb,uint32_t bytes,uint32_t tag,int last);
int i9100_mfc_decoder_acquire(struct i9100_mfc *,struct i9100_mfc_frame_lease *);
int i9100_mfc_decoder_release(struct i9100_mfc *,struct i9100_mfc_frame_lease *);
int i9100_mfc_decoder_copy_nv12(struct i9100_mfc *,const struct i9100_mfc_frame_lease *,
 void *luma,uint32_t luma_capacity,uint32_t luma_stride,void *chroma,uint32_t chroma_capacity,uint32_t chroma_stride);
/* Called only by checked core poll/reset with the engine lock held. */
int i9100_mfc_decoder_complete_locked(struct i9100_mfc *,unsigned reply);
int i9100_mfc_decoder_cleanup_locked(struct i9100_mfc *);
/* Original MFCv5.1 relative input addresses, absolute PA>>11 result
 * addresses,16slot DPB in owned arenas. Callback must verify live Normal-NC
 * aliases; host CPB/shared windows and displayed-surface reads additionally
 * require completed command/idle MC under the same engine lock. RISC/arena
 * ownership remains DEVICE; no general CPU ownership of active firmware.
 * Display-ready/lease slots are excluded from RELEASE_BUF. Exact lease
 * address+generation+epoch pins output until release; stop retains arenas
 * while leases exist. A new input is rejected while a CPB tail remains;
 * NULL/0 continues that tail without copying, and display-only never moves
 * START_BYTE_NUM. The first retained header tail needs an explicit new tag;
 * subsequent continuations use tag0. Last marks EOS and is continued until
 * an actual EMPTY response, not presumed complete after one LAST command.
 * Input tags identify supplied chunks; display_sequence preserves firmware
 * presentation order independently of decode order and physical slot.
 * No arbitrary physical addresses or cache invalidate-all. Tiled copy
 * validates both planes before writing, and reports original crop/status. */
#ifdef __cplusplus
}
#endif
#endif
