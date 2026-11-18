#ifndef I9100_SCALED_FRAME_H
#define I9100_SCALED_FRAME_H
#include <stdint.h>
/* Exact 4/3 output mapping for an integer 360x600 BGRA render canvas.
 * Physical FIMD remains 480x800. Interpolate packed channels with weights/16;
 * no floating point, allocation, source writes or unbounded input footprint. */
static inline uint32_t i9100_blend4(uint32_t a,uint32_t b,uint32_t c,uint32_t d,
 unsigned wx,unsigned wy)
{
 unsigned wa=(4U-wx)*(4U-wy),wb=wx*(4U-wy),wc=(4U-wx)*wy,wd=wx*wy;
 uint32_t rb=((a&0x00ff00ffU)*wa+(b&0x00ff00ffU)*wb+(c&0x00ff00ffU)*wc+(d&0x00ff00ffU)*wd+0x00080008U);
 uint32_t ga=(((a>>8)&0x00ff00ffU)*wa+((b>>8)&0x00ff00ffU)*wb+((c>>8)&0x00ff00ffU)*wc+((d>>8)&0x00ff00ffU)*wd+0x00080008U);
 return ((rb>>4)&0x00ff00ffU)|((ga<<4)&0xff00ff00U);
}
static inline int i9100_expand_canvas(volatile uint32_t *destination,const uint8_t *source,
 uint32_t bytes,uint32_t stride)
{
 if(!destination || !source || (((uintptr_t)destination|(uintptr_t)source)&3U) || stride<1440U || (stride&3U) ||
    (uint64_t)stride*599U+1440U>bytes)return 0;
 uintptr_t dest=(uintptr_t)destination,src=(uintptr_t)source;
 uint64_t span=(uint64_t)stride*599U+1440U;
 if(span>UINTPTR_MAX-src || 1536000U>UINTPTR_MAX-dest ||
    (src<dest+1536000U && dest<src+span))return 0;
 for(unsigned y=0;y<800;y++){
  unsigned py=y*3U,sy=py>>2,wy=py&3U,ny=sy<599?sy+1:sy;
  const uint32_t *a=(const uint32_t *)(source+(uint64_t)sy*stride);
  const uint32_t *b=(const uint32_t *)(source+(uint64_t)ny*stride);
  for(unsigned x=0;x<480;x++){
   unsigned px=x*3U,sx=px>>2,wx=px&3U,nx=sx<359?sx+1:sx;
   destination[y*480U+x]=i9100_blend4(a[sx],a[nx],b[sx],b[nx],wx,wy);
  }
 }
 return 1;
}
#endif
