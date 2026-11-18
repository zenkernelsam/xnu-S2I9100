/* SPDX-License-Identifier: BSD-2-Clause; kernel and original iOS6 pdev wire ABI. */
#ifndef I9100_AUDIO_USER_H
#define I9100_AUDIO_USER_H
#include <stdint.h>
#define I9100_AUDIO_USER_ABI 1U
#define I9100_AUDIO_USER_CLASS "IOS6I9100AudioPCM"
#define I9100_AUDIO_USER_MAX_FRAMES 512U
enum i9100_audio_user_op {
 I9100_AU_INFO=0,I9100_AU_PREPARE=1,I9100_AU_ENQUEUE=2,I9100_AU_START=3,
 I9100_AU_OUTPUT_PREPARE=4,I9100_AU_ACTIVATE=5,I9100_AU_MUTE=6,
 I9100_AU_STATUS=7,I9100_AU_STOP=8,I9100_AU_FLUSH=9
};
enum i9100_audio_user_flag {
 I9100_AU_READY=1U,I9100_AU_RUNNING=2U,I9100_AU_FAULT=4U,
 I9100_AU_OUTPUT_PREPARED=8U,I9100_AU_OUTPUT_ACTIVE=16U,
 I9100_AU_CLOSED=32U,I9100_AU_LOW_OUTPUT_ALLOWED=64U,I9100_AU_TIMESTAMP_VALID=128U,I9100_AU_GATED_DMA=256U
};
enum i9100_audio_user_error {
 I9100_AU_ARGUMENT=-1760,I9100_AU_DISABLED=-1761,I9100_AU_OWNER=-1762,
 I9100_AU_SEQUENCE=-1763,I9100_AU_OUTPUT_DENIED=-1764,I9100_AU_IO=-1765
};
struct i9100_audio_user_request {
 uint32_t abi,op,session,sequence,frames,arg0,arg1,reserved;
};
/* Explicit 32-bit halves avoid Darwin/host differences in UInt64 alignment.
 * ENQUEUE appends exactly frames*4 little-endian stereo signed16 bytes.
 * No addresses, mmap, microcode, variable gain or arbitrary MMIO are accepted.
 */
struct i9100_audio_user_reply {
 uint32_t abi,session,sequence,flags;
 int32_t error,snapshot_error,cleanup_error,last_error;
 uint32_t sample_rate,period_frames,max_frames,channels;
 uint32_t accepted_frames,staged_frames,capacity_frames,stream_seed;
 uint32_t completed_frames_lo,completed_frames_hi,completed_periods_lo,completed_periods_hi;
 uint32_t start_ns_lo,start_ns_hi,completion_ns_lo,completion_ns_hi;
 uint32_t silence_periods_lo,silence_periods_hi,output_route;
 int32_t output_gain_mdb;
 uint32_t fifo_errors,board_phase,board_stage,reserved;
};
static inline uint64_t i9100_audio_user_u64(uint32_t lo,uint32_t hi)
{return (uint64_t)lo|((uint64_t)hi<<32);}
#endif
