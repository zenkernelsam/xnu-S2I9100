/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_video_layout.h"
#include <string.h>
static uint32_t align(uint32_t v,uint32_t a){return (v+a-1U)&~(a-1U);}
int i9100_video_layout(enum i9100_video_format format,uint32_t width,uint32_t height,struct i9100_video_layout *out)
{
 if(!out||!width||!height||width>1920U||height>1080U||(width&1U)||(height&1U)||format<1||format>3)return -1;
 struct i9100_video_layout result;memset(&result,0,sizeof(result));result.width=width;result.height=height;result.format=format;
 result.plane_count=format==I9100_VIDEO_YUYV?1U:2U;
 if(format==I9100_VIDEO_NV12MT){
  result.pitch=align(width,128);result.plane_bytes[0]=align(result.pitch*align(height,32),8192);
  result.plane_bytes[1]=align(result.pitch*align(height/2U,32),8192);
 }else{result.pitch=align(width*(format==I9100_VIDEO_YUYV?2U:1U),16U);
  result.plane_bytes[0]=align(result.pitch*height,4096);result.plane_bytes[1]=format==I9100_VIDEO_NV12?align(result.pitch*(height/2U),4096):0;}
 result.plane_offset[1]=result.plane_bytes[0];result.total_bytes=result.plane_bytes[0]+result.plane_bytes[1];*out=result;return 0;
}
int i9100_video_range(const struct i9100_video_layout *layout,uint32_t plane,uint32_t offset,uint32_t bytes,uint32_t allocation)
{
 if(!layout||plane>=layout->plane_count||plane>=2U||!bytes||layout->total_bytes>allocation||
    layout->plane_offset[plane]>layout->total_bytes||layout->plane_bytes[plane]>layout->total_bytes-layout->plane_offset[plane]||
    offset>layout->plane_bytes[plane]||bytes>layout->plane_bytes[plane]-offset||
    (uint64_t)layout->plane_offset[plane]+offset+bytes>allocation)return -1;
 return 0;
}
