/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_layout.h"
#include <string.h>
static uint32_t align(uint32_t n,uint32_t a){return (n+a-1U)&~(a-1U);}
int i9100_mfc_h264_plan(uint32_t system,uint32_t bank_a,uint32_t stream,struct i9100_mfc_h264_plan *out)
{
 if(!out||(system!=0x60000U&&system!=0x80000U)||bank_a>16U*1024U*1024U||
    stream<5U||stream>I9100_MFC_CPB_BYTES)return -1;
 struct i9100_mfc_h264_plan p;memset(&p,0,sizeof(p));p.system_bytes=system;
 p.context_offset=system;p.context_bytes=I9100_MFC_H264_CONTEXT;
 p.shared_offset=align(p.context_offset+p.context_bytes,4096U);p.shared_bytes=4096U;
 p.descriptor_offset=align(p.shared_offset+p.shared_bytes,2048U);p.descriptor_bytes=I9100_MFC_DESC_BYTES;
 p.stream_offset=align(p.descriptor_offset+p.descriptor_bytes,2048U);p.stream_capacity=I9100_MFC_CPB_BYTES;
 p.stream_bytes=stream;p.end_offset=p.stream_offset+p.stream_capacity;
 if(p.end_offset>bank_a)return -1;*out=p;return 0;
}
int i9100_mfc_h264_sizes(uint32_t width,uint32_t height,uint32_t count,struct i9100_mfc_h264_sizes *out)
{
 if(!out||width<16U||height<16U||width>1920U||height>1080U||(width&1U)||(height&1U)||!count||count>16U)return -1;
 struct i9100_mfc_h264_sizes s;memset(&s,0,sizeof(s));s.width=width;s.height=height;s.dpb_count=count;
 s.pitch=align(width,128U);s.aligned_height=align(height,32U);
 s.luma_bytes=align(s.pitch*s.aligned_height,8192U);
 s.chroma_bytes=align(s.pitch*align(height/2U,32U),8192U);
 s.mv_bytes=align(s.pitch*align(s.aligned_height/4U,32U),8192U);
 s.bank_a_scratch=16384U+32768U;s.bank_a_dpb_bytes=s.bank_a_scratch+count*s.chroma_bytes;
 s.bank_b_dpb_bytes=count*(s.luma_bytes+s.mv_bytes);
 if(s.bank_a_dpb_bytes>16U*1024U*1024U||s.bank_b_dpb_bytes>16U*1024U*1024U)return -1;
 *out=s;return 0;
}
