#!/usr/bin/env python3
"""Check integer render geometry and all physical bilinear output pixels.

Production framebuffer copy/expansion and original full-frame validator run;
no GPU, phone or filesystem deletion. Keep each result under out.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess,json
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('ui-canvas-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
def function(text,name):
    start=text.index(name);start=text.rfind('\n',0,start)+1;brace=text.index('{',start);depth=0
    for at in range(brace,len(text)):
        if text[at]=='{':depth+=1
        if text[at]=='}':
            depth-=1
            if not depth:return text[start:at+1]
    raise ValueError(name)
pe=(repo/'pexpert/arm/pe_exynos4210_i9100.c').read_text()
display=(repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7LeoDisplay.cpp').read_text()
getter=function(pe,'int ios7i9100_display_render_size(')
validator=function(display,'static bool ios7lab_fullframe_scanout_layout(')
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "i9100_scaled_frame.h"
#include "i9100_framebuffer_copy.h"
static int selected,ready=1;
static int ios7i9100_display_ready(void){return ready;}
static int PE_parse_boot_argn(const char*key,int*out,unsigned n){assert(!strcmp(key,"i9100uicanvas") && n==4);*out=selected;return 1;}
enum{IOS7LabSwapMetaSurfaceZero=1,IOS7LabSwapMetaMask,IOS7LabSwapMetaLayout};
static uint32_t leoWidth=480,leoHeight=800;
static uint32_t oracle(uint32_t a,uint32_t b,uint32_t c,uint32_t d,unsigned wx,unsigned wy){
 uint32_t out=0;unsigned w[4]={(4-wx)*(4-wy),wx*(4-wy),(4-wx)*wy,wx*wy};uint32_t p[4]={a,b,c,d};
 for(unsigned ch=0;ch<4;ch++){unsigned value=8;for(unsigned n=0;n<4;n++)value+=((p[n]>>(8*ch))&255)*w[n];out|=(value/16)<<(8*ch);}return out;
}
static void cp(void*d,const void*s,uint32_t n){memcpy(d,s,n);}
'''+getter+'\n'+validator+r'''
int main(void){
 uint32_t w,h,row;for(unsigned n=0;n<3;n++){selected=n==0?0:n==1?360:123;assert(ios7i9100_display_render_size(&w,&h,&row));assert(w==(n==1?360U:480U) && h==(n==1?600U:800U) && row==w*4);}
 ready=0;assert(!ios7i9100_display_render_size(&w,&h,&row));ready=1;
 for(unsigned choice=0;choice<2;choice++){
  unsigned stride=choice?1472:1440;size_t size=(size_t)stride*599+1440;
  uint32_t*source=calloc(1,size+128);uint32_t*raw=malloc(1536000+128);assert(source&&raw);uint32_t*dst=raw+16;
  for(unsigned y=0;y<600;y++)for(unsigned x=0;x<360;x++)source[((size_t)y*stride)/4+x]=(x*7U&255U)|((y*3U&255U)<<8)|(((x+y)*5U&255U)<<16)|(0xffU<<24);
  for(unsigned n=0;n<(1536000+128)/4;n++)raw[n]=0xa5a5a5a5;
  assert(i9100_expand_canvas(dst,(const uint8_t*)source,(uint32_t)size,stride));
  for(unsigned y=0;y<800;y++)for(unsigned x=0;x<480;x++){
   unsigned sy=y*3/4,sx=x*3/4,ny=sy<599?sy+1:sy,nx=sx<359?sx+1:sx;
   uint32_t*a=source+((size_t)sy*stride)/4,*b=source+((size_t)ny*stride)/4;
   assert(dst[y*480+x]==oracle(a[sx],a[nx],b[sx],b[nx],x*3%4,y*3%4));
  }
  for(unsigned n=0;n<16;n++)assert(raw[n]==0xa5a5a5a5 && raw[16+384000+n]==0xa5a5a5a5);
  uint32_t first=dst[0];assert(!i9100_expand_canvas(dst,(const uint8_t*)source,(uint32_t)size-1,stride) && dst[0]==first);
  assert(!i9100_expand_canvas(dst,(const uint8_t*)source,size,1436));
  assert(!i9100_expand_canvas(dst,(const uint8_t*)source+1,size,stride));
  assert(!i9100_expand_canvas(source,(const uint8_t*)source,size,stride));
  assert(!i9100_expand_canvas(NULL,(const uint8_t*)source,size,stride));
  free(source);free(raw);
 }
 uint32_t words[0x138/4]={0},reason=0,mismatch=0;leoWidth=360;leoHeight=600;
 words[7]=1;words[13]=words[29]=words[53]=360;words[14]=words[30]=words[54]=600;
 words[43]=words[67]=words[68]=1;words[69]=0xff000000;
 assert(ios7lab_fullframe_scanout_layout(words,1,&reason,&mismatch));words[13]=480;
 assert(!ios7lab_fullframe_scanout_layout(words,1,&reason,&mismatch) && mismatch==13);
 uint8_t*a=calloc(1,1536000),*b=calloc(1,1536000);assert(a&&b);a[1535999]=42;
 assert(i9100_copy_frame(b,a,1536000,1920,cp) && b[1535999]==42);free(a);free(b);
 puts("PASS exact integer render-size contract, 768000 bilinear output pixels/padded rows, edge guards, no-overlap, original full-frame validation and baseline copy");
}
'''
(out/'check.c').write_text(code)
for n,cmd in enumerate((['clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
    '-I'+str(repo/'pexpert/arm'),str(out/'check.c'),'-o',str(out/'check')],[str(out/'check')])):
    r=subprocess.run(cmd,text=True,capture_output=True);(out/f'command-{n}.log').write_text(r.stdout+r.stderr)
    if r.returncode:print(r.stderr[-3000:],file=sys.stderr);r.check_returncode()
    if r.stdout:print(r.stdout.strip(),flush=True)
(out/'result.json').write_text(json.dumps(dict(result='PASS',source='Actual render-size and SwapEnd validator; production packed-pixel expansion; hardware not qualified'),indent=2)+'\n')
print('EVIDENCE',out,flush=True)
