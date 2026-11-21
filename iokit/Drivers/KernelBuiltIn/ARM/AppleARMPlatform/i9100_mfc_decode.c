/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_decode.h"
#include <string.h>
static uint32_t rd(struct i9100_mfc *m,unsigned off){return m->io.read32(m->io.cookie,off);}
static uint32_t load(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void store(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(8*n));}
static uint32_t align(uint32_t n,uint32_t a){return (n+a-1U)&~(a-1U);}
static int overlaps(uintptr_t a,uintptr_t n,uintptr_t b,uintptr_t k)
{return a>UINTPTR_MAX-n||b>UINTPTR_MAX-k||(a<b+k&&b<a+n);}
static int external(struct i9100_mfc *m,const void *p,uint32_t bytes)
{
 uintptr_t a=(uintptr_t)p;struct i9100_mfc_decoder *d=m->decoder;
 if(!p||!bytes||a>UINTPTR_MAX-bytes||overlaps(a,bytes,(uintptr_t)m,sizeof(*m))||
    overlaps(a,bytes,(uintptr_t)d,sizeof(*d))||overlaps(a,bytes,(uintptr_t)m->bank_a,sizeof(*m->bank_a))||
    overlaps(a,bytes,(uintptr_t)m->bank_b,sizeof(*m->bank_b))||
    overlaps(a,bytes,m->bank_a->cpu,m->bank_a->bytes)||overlaps(a,bytes,m->bank_b->cpu,m->bank_b->bytes))return 0;
 for(unsigned n=0;n<I9100_MFC_DPB_MAX;n++)if(d->leases[n]&&overlaps(a,bytes,(uintptr_t)d->leases[n],sizeof(*d->leases[n])))return 0;
 return 1;
}
static int valid(struct i9100_mfc *m)
{
 struct i9100_mfc_decoder *d=m?m->decoder:0;
 return !m||m->self!=(uintptr_t)m||!d||d->self!=(uintptr_t)d||d->engine!=(uintptr_t)m||
  !d->enabled||d->generation!=m->generation?I9100_MFC_OWNER:0;
}
static int enter(struct i9100_mfc *m){int e=valid(m);return e?e:m->io.lock(m->io.cookie)?I9100_MFC_BUSY:0;}
static int quiet(struct i9100_mfc *m)
{
 struct i9100_mfc_decoder *d=m->decoder;
 if(m->pending||m->fault||d->fault||d->closed||d->stopping||!m->risc_running||m->held_mask!=3U||!m->bank_a||!m->bank_b||
    m->bank_a->state!=I9100_DMA_DEVICE||m->bank_b->state!=I9100_DMA_DEVICE)return I9100_MFC_BUSY;
 if(d->io.normal_nc(d->io.cookie,m->bank_a,m->bank_b))return I9100_MFC_OWNER;
 if(rd(m,0)!=0x3ffU||rd(m,0x508)!=m->bank_a->physical||rd(m,0x50c)!=m->bank_b->physical)return I9100_MFC_OWNER;
 if(rd(m,0x30)||rd(m,0x44)||rd(m,8)||rd(m,0x510)&3U)return I9100_MFC_BUSY;
 m->io.barrier(m->io.cookie);return 0;
}
static int exact(struct i9100_mfc *m,unsigned off,uint32_t v)
{m->io.write32(m->io.cookie,off,v);m->io.barrier(m->io.cookie);return rd(m,off)==v?0:I9100_MFC_IO;}
static int track(struct i9100_mfc *m,unsigned off,uint32_t v)
{
 struct i9100_mfc_decoder *d=m->decoder;unsigned slot=d->registers;
 for(unsigned n=0;n<slot;n++)if(d->regs[n].offset==off){slot=n;break;}
 if(slot==d->registers){if(slot>=I9100_MFC_DPB_REGS)return I9100_MFC_LAYOUT;
  uint32_t saved=rd(m,off);if(saved==UINT32_MAX)return I9100_MFC_IO;d->regs[slot].offset=off;d->regs[slot].saved=saved;d->registers++;}
 d->regs[slot].written=v;return exact(m,off,v);
}
static int fail(struct i9100_mfc *m,int e)
{struct i9100_mfc_decoder *d=m->decoder;if(!d->error)d->error=e;d->fault=1;
 return i9100_mfc_fail_locked(m,e);}
int i9100_mfc_dpb_layout(const struct i9100_mfc_h264_plan *p,const struct i9100_mfc_h264_sizes *s,
 uint32_t a,uint32_t b,struct i9100_mfc_dpb_layout *out)
{
 if(!p||!s||!out||a>16U*1024U*1024U||b>16U*1024U*1024U)return I9100_MFC_LAYOUT;
 struct i9100_mfc_h264_plan expected;struct i9100_mfc_h264_sizes sizes;
 if(i9100_mfc_h264_plan(p->system_bytes,a,p->stream_bytes,&expected)||memcmp(p,&expected,sizeof(expected))||
    i9100_mfc_h264_sizes(s->width,s->height,s->dpb_count,&sizes)||memcmp(s,&sizes,sizeof(sizes)))return I9100_MFC_LAYOUT;
 struct i9100_mfc_dpb_layout l;memset(&l,0,sizeof(l));l.sizes=*s;l.count=s->dpb_count;
 l.scratch_mv=align(p->end_offset,8192U);l.scratch_ip=l.scratch_mv+16384U;
 uint64_t ca=align(l.scratch_ip+32768U,8192U),cb=0;
 for(unsigned n=0;n<l.count;n++){
  if(ca+s->chroma_bytes>a||cb+s->luma_bytes+s->mv_bytes>b)return I9100_MFC_LAYOUT;
  l.chroma[n]=(uint32_t)ca;l.luma[n]=(uint32_t)cb;l.mv[n]=(uint32_t)cb+s->luma_bytes;
  ca+=s->chroma_bytes;cb+=s->luma_bytes+s->mv_bytes;
 }
 l.end_a=(uint32_t)ca;l.end_b=(uint32_t)cb;*out=l;return 0;
}
int i9100_mfc_decoder_bind(int enabled,struct i9100_mfc *m,struct i9100_mfc_decoder *d,const struct i9100_mfc_decode_io *io)
{
 if(!enabled)return I9100_MFC_DISABLED;
 if(enabled!=1||!m||m->self!=(uintptr_t)m||!d||m->decoder||!io||!io->normal_nc||!m->generation||
    overlaps((uintptr_t)d,sizeof(*d),(uintptr_t)m,sizeof(*m))||
    overlaps((uintptr_t)d,sizeof(*d),(uintptr_t)io,sizeof(*io)))return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;int e=0;
 if(d->self||m->pending||m->fault||!m->risc_running||!m->instance_open||m->state!=3U)e=I9100_MFC_BUSY;
 else if(!m->bank_a||!m->bank_b||overlaps((uintptr_t)d,sizeof(*d),m->bank_a->cpu,m->bank_a->bytes)||
         overlaps((uintptr_t)d,sizeof(*d),m->bank_b->cpu,m->bank_b->bytes)||
         overlaps((uintptr_t)d,sizeof(*d),(uintptr_t)m->bank_a,sizeof(*m->bank_a))||
         overlaps((uintptr_t)d,sizeof(*d),(uintptr_t)m->bank_b,sizeof(*m->bank_b))||m->consumed_bytes>m->plan.stream_bytes)e=I9100_MFC_ARGUMENT;
 else if(io->normal_nc(io->cookie,m->bank_a,m->bank_b))e=I9100_MFC_OWNER;
 if(!e){memset(d,0,sizeof(*d));d->self=(uintptr_t)d;d->engine=(uintptr_t)m;d->generation=m->generation;d->enabled=1;d->io=*io;
  d->input_bytes=m->plan.stream_bytes;d->input_start=m->consumed_bytes;memcpy(d->input_sha256,m->stream_sha256,32);m->decoder=d;}
 m->io.unlock(m->io.cookie);return e;
}
static void issue(struct i9100_mfc *m,unsigned command,unsigned reply)
{m->pending=1;m->expected=reply;m->started=m->io.now_ns(m->io.cookie);m->polls=0;
 m->last_channel0=(command<<16)|m->instance;m->io.write32(m->io.cookie,0x2040,m->last_channel0);m->io.barrier(m->io.cookie);}
int i9100_mfc_decoder_init_buffers(struct i9100_mfc *m)
{
 int e=enter(m);if(e)return e;struct i9100_mfc_decoder *d=m->decoder;
 if(d->configured||m->state!=3U){e=I9100_MFC_BUSY;goto out;}e=quiet(m);if(e)goto out;
 e=i9100_mfc_dpb_layout(&m->plan,&m->sequence,m->bank_a->bytes,m->bank_b->bytes,&d->layout);if(e)goto out;
 /* Firmware is live; only proved Normal-NC host/scratch/DPB windows are
  * touched while its previous command and MC transaction are complete. */
 memset((void *)(m->bank_a->cpu+d->layout.scratch_mv),0,d->layout.end_a-d->layout.scratch_mv);
 memset((void *)m->bank_b->cpu,0,d->layout.end_b);
 for(unsigned n=0;n<d->layout.count;n++)memset((void *)(m->bank_a->cpu+d->layout.chroma[n]),0x80,m->sequence.chroma_bytes);
 e=track(m,0x68c,d->layout.scratch_mv>>11);if(!e)e=track(m,0x690,d->layout.scratch_ip>>11);
 for(unsigned n=0;n<d->layout.count&&!e;n++){
  e=track(m,0x600+4*n,d->layout.chroma[n]>>11);if(!e)e=track(m,0x700+4*n,d->layout.luma[n]>>11);
  if(!e)e=track(m,0x780+4*n,d->layout.mv[n]>>11);
 }
 if(!e)e=track(m,0x2068,d->layout.count);if(!e)e=track(m,0x2060,0);
 if(!e)e=exact(m,0x2064,m->plan.shared_offset);if(e){fail(m,e);goto out;}
 uint8_t *shared=(uint8_t *)(m->bank_a->cpu+m->plan.shared_offset);
 store(shared+0x64,m->sequence.luma_bytes);store(shared+0x68,m->sequence.chroma_bytes);store(shared+0x6c,m->sequence.mv_bytes);
 m->io.barrier(m->io.cookie);
 if(load(shared+0x64)!=m->sequence.luma_bytes||load(shared+0x68)!=m->sequence.chroma_bytes||load(shared+0x6c)!=m->sequence.mv_bytes)
  {e=fail(m,I9100_MFC_IO);goto out;}
 d->configured=1;issue(m,4,15);
out:m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_decoder_frame(struct i9100_mfc *m,const void *data,uint32_t bytes,uint32_t tag,int last)
{
 int e=enter(m);if(e)return e;struct i9100_mfc_decoder *d=m->decoder;
 if((last!=0&&last!=1)||(data==0)!=(bytes==0)||bytes>m->plan.stream_capacity||d->input_start>d->input_bytes||
    (bytes&&(!tag||tag<=d->last_tag||bytes<5U))||d->frame_submitted==UINT32_MAX){e=I9100_MFC_ARGUMENT;goto out;}
 if(!d->ready||d->drained||(d->draining&&(!last||bytes))){e=I9100_MFC_BUSY;goto out;}e=quiet(m);if(e)goto out;
 uint32_t remaining=d->input_bytes-d->input_start;
 if(bytes&&remaining){e=I9100_MFC_BUSY;goto out;}
 if(!bytes&&((!remaining&&!last)||(!remaining&&tag)||(remaining&&d->input_tag&&tag)||
    (remaining&&!d->input_tag&&(!tag||tag<=d->last_tag)))){e=I9100_MFC_ARGUMENT;goto out;}
 uint32_t available=((1U<<d->layout.count)-1U)&~(d->display_ready|d->leased);
 if(!available){e=I9100_MFC_BUSY;goto out;}
 if(data){
  if(!external(m,data,bytes)){e=I9100_MFC_ARGUMENT;goto out;}
  const uint8_t *p=data;if(p[0]||p[1]||(p[2]!=1U&&(p[2]||p[3]!=1U))){e=I9100_MFC_ARGUMENT;goto out;}
  uint8_t digest[32];if(m->io.sha256(m->io.cookie,data,bytes,digest)){e=I9100_MFC_HASH;goto out;}
  memcpy((void *)(m->bank_a->cpu+m->plan.stream_offset),data,bytes);
  if(m->io.sha256(m->io.cookie,(const void *)(m->bank_a->cpu+m->plan.stream_offset),bytes,d->input_sha256)||
     memcmp(digest,d->input_sha256,32)){e=fail(m,I9100_MFC_HASH);goto out;}
  d->input_bytes=bytes;d->input_start=0;d->input_tag=tag;d->last_tag=tag;remaining=bytes;
 }else if(remaining){
  uint8_t digest[32];if(m->io.sha256(m->io.cookie,(const void *)(m->bank_a->cpu+m->plan.stream_offset),d->input_bytes,digest)||
      memcmp(digest,d->input_sha256,32)){e=fail(m,I9100_MFC_HASH);goto out;}
  if(!d->input_tag)d->input_tag=d->last_tag=tag;
 }
 unsigned command=remaining?2U:3U;
 if(command==3U&&d->drain_commands>=d->layout.count+2U){e=fail(m,I9100_MFC_TIMEOUT);goto out;}
 uint8_t *shared=(uint8_t *)(m->bank_a->cpu+m->plan.shared_offset);store(shared+4,d->input_tag);store(shared+0x18,d->input_start);
 e=exact(m,0x2044,m->plan.stream_offset>>11);if(!e)e=exact(m,0x2048,remaining);
 if(!e)e=exact(m,0x204c,m->plan.descriptor_offset>>11);if(!e)e=exact(m,0x2058,m->plan.stream_capacity);
 if(!e)e=exact(m,0x205c,m->plan.descriptor_bytes);if(!e)e=exact(m,0x2064,m->plan.shared_offset);
 if(!e)e=track(m,0x2060,available);if(!e)e=track(m,0x2068,d->layout.count);
 if(e){fail(m,e);goto out;}m->io.barrier(m->io.cookie);
 d->available=d->submitted_release=available;d->submitted_bytes=remaining;d->submitted_start=d->input_start;
 d->submitted_tag=d->input_tag;d->submitted_last=command==3U;d->draining|=(unsigned)last;
 if(command==3U)d->drain_commands++;d->frame_submitted++;issue(m,command,5);
out:m->io.unlock(m->io.cookie);return e;
}
static int slot_pair(struct i9100_mfc *m,uint32_t y,uint32_t c)
{
 struct i9100_mfc_decoder *d=m->decoder;uint64_t yp=(uint64_t)y<<11,cp=(uint64_t)c<<11;
 if(yp>UINT32_MAX||cp>UINT32_MAX)return -1;
 for(unsigned n=0;n<d->layout.count;n++)if(yp==(uint64_t)m->bank_b->physical+d->layout.luma[n]&&
    cp==(uint64_t)m->bank_a->physical+d->layout.chroma[n])return (int)n;
 return -1;
}
int i9100_mfc_decoder_complete_locked(struct i9100_mfc *m,unsigned reply)
{
 int e=valid(m);if(e)return e;struct i9100_mfc_decoder *d=m->decoder;
 if(!m->pending||m->expected!=reply||d->closed||d->stopping)return I9100_MFC_REPLY;
 if(rd(m,0x510)&3U)return fail(m,I9100_MFC_OWNER);
 {int nc=d->io.normal_nc(d->io.cookie,m->bank_a,m->bank_b);
  if(nc==I9100_MFC_BUSY)return I9100_MFC_BUSY; /* gate contention: leave the reply for a retry */
  if(nc)return fail(m,I9100_MFC_OWNER);}
 if(reply==15U){
  if(!d->configured||d->ready||rd(m,0x2000)!=m->instance)return fail(m,I9100_MFC_REPLY);
  d->ready=1;d->available=(1U<<d->layout.count)-1U;return 0;
 }
 if(reply!=5U||!d->ready||d->frame_completed>=d->frame_submitted)return I9100_MFC_REPLY;
 const unsigned offsets[9]={0x201c,0x202c,0x2020,0x2000,0x2018,0x2010,0x2014,0x2024,0x2028};uint32_t raw[9];
 for(unsigned n=0;n<9;n++){raw[n]=rd(m,offsets[n]);if(raw[n]==UINT32_MAX)return fail(m,I9100_MFC_IO);}
 d->raw_display=raw[0];d->raw_decoded=raw[1];d->raw_type=raw[2];d->raw_channel=raw[3];d->raw_consumed=raw[4];
 d->raw_display_y=raw[5];d->raw_display_c=raw[6];d->raw_decoded_y=raw[7];d->raw_decoded_c=raw[8];
 m->io.barrier(m->io.cookie);for(unsigned n=0;n<9;n++)if(rd(m,offsets[n])!=raw[n])return fail(m,I9100_MFC_REPLY);
 unsigned display=raw[0]&7U,decode=raw[1]&7U,type=raw[2]&7U;
 if(display>3U||decode>4U||type>4U||raw[3]!=m->instance||raw[4]>d->submitted_bytes||
    raw[0]&((1U<<3)|(3U<<4))||raw[1]&(1U<<3))return fail(m,I9100_MFC_REPLY);
 if((display==2U&&raw[4])||(display==3U&&(!d->submitted_last||d->submitted_bytes||raw[4])))return fail(m,I9100_MFC_REPLY);
 uint8_t *shared=(uint8_t *)(m->bank_a->cpu+m->plan.shared_offset);
 uint32_t tag=load(shared+8),crop_h=load(shared+0x20),crop_v=load(shared+0x24),display_type=load(shared+0xc0);
 d->raw_tag=tag;d->raw_crop_h=crop_h;d->raw_crop_v=crop_v;d->raw_display_type=display_type;
 m->io.barrier(m->io.cookie);
 if(tag!=load(shared+8)||crop_h!=load(shared+0x20)||crop_v!=load(shared+0x24)||display_type!=load(shared+0xc0))return fail(m,I9100_MFC_REPLY);
 unsigned shown_type=(display_type>>2)&7U;if(shown_type>4U)return fail(m,I9100_MFC_REPLY);
 int decoded=-1,shown=-1;
 if((display==0U||display==1U)&&type&&(decode==0U||decode==1U)){
  decoded=slot_pair(m,raw[7],raw[8]);if(decoded<0||!d->submitted_tag||
     !(d->submitted_release&(1U<<decoded))||d->frames[decoded].epoch==UINT32_MAX)
   return fail(m,I9100_MFC_REPLY);
 }
 if(shown_type&&(display==1U||display==2U)){
  shown=slot_pair(m,raw[5],raw[6]);if(shown<0||(d->display_ready|d->leased)&(1U<<shown)||
     (shown!=decoded&&!d->frames[shown].epoch))return fail(m,I9100_MFC_REPLY);
  uint32_t expected=shown==decoded?d->submitted_tag:d->frames[shown].tag;
  if(!tag||tag!=expected||((crop_h&65535U)+(crop_h>>16))>=m->sequence.width||
     ((crop_v&65535U)+(crop_v>>16))>=m->sequence.height)return fail(m,I9100_MFC_REPLY);
 }
 if(shown>=0&&d->displayed==UINT32_MAX)return fail(m,I9100_MFC_REPLY);
 if(decoded<0&&shown<0&&!raw[4]&&display!=3U&&d->no_progress>=32U)return fail(m,I9100_MFC_TIMEOUT);
 if(decoded>=0){struct i9100_mfc_frame *f=&d->frames[decoded];f->slot=(unsigned)decoded;f->epoch++;f->tag=d->submitted_tag;
  f->display_sequence=0;
  f->width=m->sequence.width;f->height=m->sequence.height;f->pitch=m->sequence.pitch;f->luma_bytes=m->sequence.luma_bytes;f->chroma_bytes=m->sequence.chroma_bytes;}
 if(shown>=0){struct i9100_mfc_frame *f=&d->frames[shown];f->display_status=raw[0];f->decode_status=raw[1];f->frame_type=shown_type;
  f->crop_left=crop_h&65535U;f->crop_right=crop_h>>16;f->crop_top=crop_v&65535U;f->crop_bottom=crop_v>>16;
  f->display_sequence=++d->displayed;d->display_ready|=1U<<shown;}
 if(display!=2U)d->input_start+=raw[4];
 if(decoded>=0||shown>=0||raw[4]||display==3U)d->no_progress=0;else d->no_progress++;
 /* consumed_bytes mirrors the accumulated stream position (decoder open
  * resumes input_start from it); raw_consumed already reports this reply's
  * delta separately. */
 if(display==3U)d->drained=1;d->frame_completed++;m->consumed_bytes=d->input_start;return 0;
}
static int lease_valid(struct i9100_mfc *m,const struct i9100_mfc_frame_lease *l)
{
 struct i9100_mfc_decoder *d=m->decoder;if(!l)return I9100_MFC_OWNER;
 unsigned slot=0;while(slot<d->layout.count&&d->leases[slot]!=l)slot++;
 if(slot==d->layout.count)return I9100_MFC_OWNER;
 return !l->owned||l->self!=(uintptr_t)l||l->engine!=(uintptr_t)m||l->generation!=d->generation||
   l->frame.slot!=slot||memcmp(&l->frame,&d->frames[slot],sizeof(l->frame))?I9100_MFC_OWNER:0;
}
int i9100_mfc_decoder_acquire(struct i9100_mfc *m,struct i9100_mfc_frame_lease *l)
{
 int e=enter(m);if(e)return e;struct i9100_mfc_decoder *d=m->decoder;
 if(!external(m,l,sizeof(*l))||l->owned||l->self||l->engine){e=I9100_MFC_ARGUMENT;goto out;}e=quiet(m);if(e)goto out;
 unsigned slot=d->layout.count;uint32_t sequence=UINT32_MAX;
 for(unsigned n=0;n<d->layout.count;n++)if((d->display_ready&(1U<<n))&&d->frames[n].display_sequence<=sequence){slot=n;sequence=d->frames[n].display_sequence;}
 if(slot==d->layout.count){e=I9100_MFC_BUSY;goto out;}
 memset(l,0,sizeof(*l));l->self=(uintptr_t)l;l->engine=(uintptr_t)m;l->generation=d->generation;l->owned=1;l->frame=d->frames[slot];
 d->display_ready&=~(1U<<slot);d->leased|=1U<<slot;d->leases[slot]=l;
out:m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_decoder_release(struct i9100_mfc *m,struct i9100_mfc_frame_lease *l)
{
 int e=enter(m);if(e)return e;struct i9100_mfc_decoder *d=m->decoder;e=lease_valid(m,l);
 if(!e){unsigned slot=l->frame.slot;d->leased&=~(1U<<slot);d->leases[slot]=0;l->owned=0;d->released++;}
 m->io.unlock(m->io.cookie);return e;
}
static int tile_copy(const uint8_t *src,uint32_t source_bytes,uint32_t pitch,uint32_t rows,uint8_t *dst,uint32_t stride,uint32_t width,uint32_t height)
{
 if((pitch&127U)||(rows&31U)||!rows||rows<height||pitch<width||stride<width)return I9100_MFC_LAYOUT;unsigned xt=pitch/64U,yt=rows/32U;
 for(unsigned y=0;y<height;y++){
  for(unsigned x=0;x<width;x+=64U){
  unsigned tx=x/64U,ty=y/32U,tile=(ty&~1U)*xt+tx;
  if(ty&1U)tile+=2U+(tx&~3U);else if(!(yt&1U)||ty!=yt-1U)tile+=(tx+2U)&~3U;
  uint64_t offset=(uint64_t)tile*2048U+(y&31U)*64U;unsigned n=width-x;if(n>64U)n=64U;
  if(offset+n>source_bytes)return I9100_MFC_LAYOUT;if(dst)memcpy(dst+(uint64_t)y*stride+x,src+offset,n);
  }
 }
 return 0;
}
int i9100_mfc_decoder_copy_nv12(struct i9100_mfc *m,const struct i9100_mfc_frame_lease *l,void *y,uint32_t yc,uint32_t ys,void *c,uint32_t cc,uint32_t cs)
{
 int e=enter(m);if(e)return e;e=lease_valid(m,l);if(!e)e=quiet(m);
 if(!e){unsigned w=l->frame.width,h=l->frame.height,n=l->frame.slot;
  uintptr_t yp=(uintptr_t)y,cp=(uintptr_t)c;
  if(!external(m,y,yc)||!external(m,c,cc)||overlaps(yp,yc,cp,cc)||
     overlaps(yp,yc,(uintptr_t)l,sizeof(*l))||overlaps(cp,cc,(uintptr_t)l,sizeof(*l))||
     !w||h<2U||ys<w||cs<w||(uint64_t)ys*(h-1U)+w>yc||(uint64_t)cs*(h/2U-1U)+w>cc)e=I9100_MFC_ARGUMENT;
  else {struct i9100_mfc_decoder *d=m->decoder;
   const uint8_t *sy=(const uint8_t *)(m->bank_b->cpu+d->layout.luma[n]),*sc=(const uint8_t *)(m->bank_a->cpu+d->layout.chroma[n]);
   e=tile_copy(sy,l->frame.luma_bytes,l->frame.pitch,m->sequence.aligned_height,0,ys,w,h);
   if(!e)e=tile_copy(sc,l->frame.chroma_bytes,l->frame.pitch,align(h/2U,32U),0,cs,w,h/2U);
   if(!e){tile_copy(sy,l->frame.luma_bytes,l->frame.pitch,m->sequence.aligned_height,y,ys,w,h);
    tile_copy(sc,l->frame.chroma_bytes,l->frame.pitch,align(h/2U,32U),c,cs,w,h/2U);}
  }
 }m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_decoder_cleanup_locked(struct i9100_mfc *m)
{
 if(!m->decoder)return 0;int e=valid(m);if(e)return e;struct i9100_mfc_decoder *d=m->decoder;
 d->stopping=1;d->ready=0;
 if(d->leased)return I9100_MFC_BUSY;
 if(m->risc_running||rd(m,0)&1U||rd(m,0x510)&3U)return I9100_MFC_BUSY;
 for(unsigned n=d->registers;n;n--){struct i9100_mfc_decode_reg *r=&d->regs[n-1];uint32_t v=rd(m,r->offset);
  if(v!=r->saved&&v!=r->written&&v){d->cleanup_error=I9100_MFC_OWNER;return d->cleanup_error;}
  e=exact(m,r->offset,r->saved);if(e){d->cleanup_error=e;return e;}
 }
 d->closed=1;d->ready=0;d->display_ready=0;d->enabled=0;m->decoder=0;return 0;
}
