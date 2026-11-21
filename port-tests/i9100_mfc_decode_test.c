/* SPDX-License-Identifier: BSD-2-Clause
 * Explicit firmware/register/NC-memory model; never a codec execution. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include "i9100_mfc_decode.h"
#include "i9100_mfc_tile_model.h"
struct model {uint32_t regs[0x3000/4],locked,writes,drop,drop_count,unstable,unstable_reads,nc_bad,hash_bad,reset_stuck;uint64_t ns;};
static struct model sim;
static struct i9100_mfc core;
static struct i9100_mfc_decoder dec;
static uint8_t mem_a[8U*1024U*1024U] __attribute__((aligned(131072)));
static uint8_t mem_b[8U*1024U*1024U] __attribute__((aligned(131072)));
static uint8_t fw[262144];
static const uint8_t stream[16]={0,0,0,1,0x67,0x42,0,0x1e,0,0,0,1,0x65,0x11,0x22,0x33};
struct block {struct i9100_dma_buffer b;unsigned lock;};
static struct block banks[2];
static uint32_t rd(void *p,unsigned off){struct model *m=p;assert(off<0x3000&&!(off&3U));
 uint32_t v=m->regs[off/4];if(m->unstable&&m->unstable==off&&++m->unstable_reads==2)v^=1;return v;}
static void wr(void *p,unsigned off,uint32_t v){struct model *m=p;assert(m->locked);m->writes++;
 if(m->drop_count&&m->drop==off){m->drop_count--;return;}m->regs[off/4]=v;
 if(off==0&&v!=0x3ffU&&!m->reset_stuck)m->regs[0x510/4]=0;
 if(off==0x30&&v)m->regs[off/4]=0;
}
static int take(void *p){struct model *m=p;if(m->locked)return -1;m->locked=1;return 0;}
static void drop(void *p){struct model *m=p;assert(m->locked);m->locked=0;}
static uint64_t now(void *p){return ((struct model *)p)->ns;}
static int delay(void *p,uint32_t us){((struct model *)p)->ns+=us*1000ULL;return 0;}
static void barrier(void *p){(void)p;}
static int hash(void *p,const void *bytes,uint32_t n,uint8_t out[32]){struct model *m=p;assert(m->locked);
 if(m->hash_bad)return -1;return SHA256(bytes,n,out)?0:-1;}
static int b_take(void *p){struct block *b=p;if(b->lock)return -1;b->lock=1;return 0;}
static void b_drop(void *p){struct block *b=p;assert(b->lock);b->lock=0;}
static int b_sync(void *p,uintptr_t cpu,uint32_t pa,uint32_t n,unsigned action){struct block *b=p;
 assert(b->lock&&cpu==b->b.cpu&&pa==b->b.physical&&n==b->b.bytes&&action>=1&&action<=4);return 0;}
static int b_idle(void *p){(void)p;return i9100_mfc_idle(&core);}
static void b_free(void *p,uintptr_t cpu,uint32_t pa,uint32_t n){(void)p;(void)cpu;(void)pa;(void)n;assert(!b_idle(p));}
static int nc(void *p,const struct i9100_dma_buffer *a,const struct i9100_dma_buffer *b){struct model *m=p;assert(m->locked);
 return m->nc_bad||a!=&banks[0].b||b!=&banks[1].b?-1:0;}
static void response(unsigned reply,unsigned arg){sim.regs[0x44/4]=reply;sim.regs[8/4]=1;
 sim.regs[0x48/4]=arg;sim.regs[0x4c/4]=sim.regs[0x50/4]=sim.regs[0x54/4]=0;sim.regs[0x58/4]=0x211116;}
static void header(unsigned width,unsigned height,unsigned count,unsigned consumed)
{
 assert(!core.bank_a&&!core.decoder&&!banks[0].b.holds&&!banks[1].b.holds);
 memset(&sim,0,sizeof(sim));memset(&core,0,sizeof(core));memset(&dec,0,sizeof(dec));sim.ns=1000000;
 struct i9100_mfc_io io={&sim,rd,wr,take,drop,now,delay,barrier,hash};assert(!i9100_mfc_init(&core,&io));
 for(unsigned i=0;i<2;i++){memset(&banks[i],0,sizeof(banks[i]));struct i9100_dma_io bio={&banks[i],b_take,b_drop,b_sync,b_idle,b_free};
  assert(!i9100_dma_bind(&banks[i].b,&bio,(uintptr_t)(i?mem_b:mem_a),i?0x48000000U:0x46000000U,8U*1024U*1024U));
  assert(!i9100_dma_enable(&banks[i].b,1));}
 assert(!i9100_mfc_enable(&core,1,1));for(unsigned n=0;n<sizeof(fw);n++)fw[n]=(uint8_t)(n*13U+7U);
 struct i9100_mfc_firmware_profile profile={0};profile.board_data_reviewed=1;profile.system_bytes=0x80000;
 profile.firmware_bytes=sizeof(fw);profile.expected_version=0x211116;profile.timeout_ms=500;assert(SHA256(fw,sizeof(fw),profile.sha256));
 assert(!i9100_mfc_stage_firmware(&core,&banks[0].b,&banks[1].b,&profile,fw,sizeof(fw)));
 assert(!i9100_mfc_stage_h264(&core,stream,sizeof(stream)));assert(!i9100_mfc_boot(&core));
 response(9,0);assert(!i9100_mfc_poll(&core));assert(!i9100_mfc_sys_init(&core));response(8,0x60000);assert(!i9100_mfc_poll(&core));
 assert(!i9100_mfc_open_h264(&core));response(1,2);assert(!i9100_mfc_poll(&core));assert(!i9100_mfc_parse_h264_header(&core));
 sim.regs[0x2000/4]=2;sim.regs[0x2008/4]=width;sim.regs[0x2004/4]=height;sim.regs[0x200c/4]=count;sim.regs[0x2018/4]=consumed;
 response(4,0);assert(!i9100_mfc_poll(&core));
}
static void ready(unsigned width,unsigned height,unsigned count,unsigned consumed)
{header(width,height,count,consumed);struct i9100_mfc_decode_io io={&sim,nc};
 assert(i9100_mfc_decoder_bind(0,&core,&dec,&io)==I9100_MFC_DISABLED);
 assert(!i9100_mfc_decoder_bind(1,&core,&dec,&io));assert(!i9100_mfc_decoder_init_buffers(&core));
 assert(core.pending&&core.expected==15&&sim.regs[0x2040/4]==0x40002U);
 for(unsigned i=0;i<count;i++){assert(sim.regs[(0x600+4*i)/4]==dec.layout.chroma[i]>>11);
  assert(sim.regs[(0x700+4*i)/4]==dec.layout.luma[i]>>11);assert(sim.regs[(0x780+4*i)/4]==dec.layout.mv[i]>>11);}
 sim.regs[0x2000/4]=2;response(15,0);assert(!i9100_mfc_poll(&core)&&dec.ready);}
static void st(unsigned off,uint32_t value){uint8_t *p=mem_a+core.plan.shared_offset+off;
 for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static void frame_response(unsigned ds,unsigned dd,unsigned dt,int shown,int decoded,unsigned used,unsigned tag,unsigned display_type)
{
 sim.regs[0x2000/4]=2;sim.regs[0x201c/4]=ds;sim.regs[0x202c/4]=dd;sim.regs[0x2020/4]=dt;sim.regs[0x2018/4]=used;
 sim.regs[0x2010/4]=shown<0?0:(banks[1].b.physical+dec.layout.luma[shown])>>11;
 sim.regs[0x2014/4]=shown<0?0:(banks[0].b.physical+dec.layout.chroma[shown])>>11;
 sim.regs[0x2024/4]=decoded<0?0:(banks[1].b.physical+dec.layout.luma[decoded])>>11;
 sim.regs[0x2028/4]=decoded<0?0:(banks[0].b.physical+dec.layout.chroma[decoded])>>11;
 st(8,tag);st(0x20,0);st(0x24,0);st(0xc0,display_type<<2);response(5,0);
}
static void finish(void){assert(!i9100_mfc_stop(&core));assert(!core.bank_a&&!core.decoder&&!banks[0].b.holds&&!banks[1].b.holds);}
static void reorder_tail(void)
{
 ready(640,480,4,8);assert(dec.input_start==8&&dec.input_bytes==16);
 assert(i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0)==I9100_MFC_BUSY);
 assert(!i9100_mfc_decoder_frame(&core,0,0,1,0));assert(sim.regs[0x2048/4]==8);
 frame_response(0,0,1,-1,2,8,0,0);assert(!i9100_mfc_poll(&core)&&dec.input_start==16);
 assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),2,0));frame_response(0,0,2,-1,0,8,0,0);
 assert(!i9100_mfc_poll(&core)&&dec.input_start==8);
 assert(i9100_mfc_decoder_frame(&core,stream,sizeof(stream),3,0)==I9100_MFC_BUSY);
 assert(!i9100_mfc_decoder_frame(&core,0,0,0,0));assert(sim.regs[0x2048/4]==8);
 frame_response(2,4,0,2,-1,0,1,1);assert(!i9100_mfc_poll(&core)&&dec.input_start==8);
 struct i9100_mfc_frame_lease first={0};assert(!i9100_mfc_decoder_acquire(&core,&first)&&first.frame.slot==2&&first.frame.tag==1);
 assert(!i9100_mfc_decoder_frame(&core,0,0,0,0));assert(!(sim.regs[0x2060/4]&(1U<<2)));
 frame_response(1,0,3,0,1,8,2,2);assert(!i9100_mfc_poll(&core)&&dec.frames[1].tag==2&&dec.input_start==16);
 assert(!i9100_mfc_decoder_release(&core,&first));assert(i9100_mfc_decoder_release(&core,&first)==I9100_MFC_OWNER);
 assert(!i9100_mfc_decoder_frame(&core,0,0,0,1));assert(sim.regs[0x2040/4]==0x30002);
 frame_response(2,4,0,1,-1,0,2,3);assert(!i9100_mfc_poll(&core)&&dec.draining&&!dec.drained);
 struct i9100_mfc_frame_lease second={0},third={0};assert(!i9100_mfc_decoder_acquire(&core,&second)&&second.frame.slot==0);
 assert(!i9100_mfc_decoder_acquire(&core,&third)&&third.frame.slot==1&&second.frame.display_sequence<third.frame.display_sequence);
 assert(!i9100_mfc_decoder_frame(&core,0,0,0,1));frame_response(3,4,0,-1,-1,0,0,0);assert(!i9100_mfc_poll(&core)&&dec.drained);
 assert(i9100_mfc_decoder_frame(&core,stream,sizeof(stream),3,0)==I9100_MFC_BUSY);
 assert(!i9100_mfc_decoder_release(&core,&second));assert(!i9100_mfc_decoder_release(&core,&third));finish();
}
/* Expected tiled layout is built independently from 2x2 cell coordinates,
 * not by reusing the production tile-index expression. */
static uint8_t sample(unsigned plane,unsigned x,unsigned y){return (uint8_t)(x*17U+y*29U+plane*47U+(x/64U)*3U+(y/32U)*5U);}
static void populate(uint8_t *p,unsigned bytes,unsigned pitch,unsigned rows,unsigned plane)
{memset(p,0xa5,bytes);unsigned xt=pitch/64U,yt=rows/32U;
 for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<pitch;x++){unsigned off=tile(x/64U,y/32U,xt,yt)*2048U+(y%32U)*64U+x%64U;
  assert(off<bytes);p[off]=sample(plane,x,y);}}
static uint8_t out_y[2048U*1088U],out_c[2048U*544U];
static void tiling(unsigned w,unsigned h)
{
 ready(w,h,2,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(1,0,1,0,0,sizeof(stream),1,1);assert(!i9100_mfc_poll(&core));
 struct i9100_mfc_frame_lease lease={0};assert(!i9100_mfc_decoder_acquire(&core,&lease));
 unsigned pitch=lease.frame.pitch,rows=core.sequence.aligned_height,chrows=(h/2U+31U)&~31U;
 populate(mem_b+dec.layout.luma[0],lease.frame.luma_bytes,pitch,rows,0);
 populate(mem_a+dec.layout.chroma[0],lease.frame.chroma_bytes,pitch,chrows,1);
 memset(out_y,0xcc,sizeof(out_y));memset(out_c,0xdd,sizeof(out_c));unsigned stride=w+14U;
 assert(!i9100_mfc_decoder_copy_nv12(&core,&lease,out_y,sizeof(out_y),stride,out_c,sizeof(out_c),stride));
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<stride;x++)assert(out_y[y*stride+x]==(x<w?sample(0,x,y):0xcc));
 for(unsigned y=0;y<h/2U;y++)for(unsigned x=0;x<stride;x++)assert(out_c[y*stride+x]==(x<w?sample(1,x,y):0xdd));
 memset(out_y,0x33,sizeof(out_y));memset(out_c,0x44,sizeof(out_c));
 assert(i9100_mfc_decoder_copy_nv12(&core,&lease,out_y,sizeof(out_y),stride,out_c,1,stride)==I9100_MFC_ARGUMENT);
 assert(out_y[0]==0x33&&out_c[0]==0x44);
 assert(i9100_mfc_decoder_copy_nv12(&core,&lease,&core,sizeof(core),stride,out_c,sizeof(out_c),stride)==I9100_MFC_ARGUMENT);
 struct i9100_mfc_frame_lease copied=lease;assert(i9100_mfc_decoder_release(&core,&copied)==I9100_MFC_OWNER);
 lease.frame.tag++;assert(i9100_mfc_decoder_release(&core,&lease)==I9100_MFC_OWNER);lease.frame.tag--;
 assert(!i9100_mfc_decoder_release(&core,&lease));finish();
}
static void failures(void)
{
 ready(640,480,4,sizeof(stream));unsigned writes=sim.writes;
 assert(i9100_mfc_decoder_frame(&core,&core,sizeof(core),1,0)==I9100_MFC_ARGUMENT&&sim.writes==writes);
 sim.nc_bad=1;assert(i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0)==I9100_MFC_OWNER&&sim.writes==writes);sim.nc_bad=0;finish();
 ready(640,480,4,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(1,0,1,0,0,sizeof(stream),1,1);sim.regs[0x2010/4]=dec.layout.luma[0]>>11; /* Wrong: relative result address. */
 assert(i9100_mfc_poll(&core)==I9100_MFC_REPLY&&core.first_error==I9100_MFC_REPLY&&!core.bank_a);finish();
 ready(640,480,4,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(1,0,1,0,0,sizeof(stream),1,1);assert(!i9100_mfc_poll(&core));struct i9100_mfc_frame_lease l={0};assert(!i9100_mfc_decoder_acquire(&core,&l));
 assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),2,0));frame_response(1,0,1,0,0,sizeof(stream),2,1);
 assert(i9100_mfc_poll(&core)==I9100_MFC_REPLY&&core.decoder==&dec&&core.held_mask==3&&banks[0].b.state==I9100_DMA_DEVICE&&dec.stopping);
 assert(i9100_mfc_stop(&core)==I9100_MFC_BUSY&&core.bank_a);assert(!i9100_mfc_decoder_release(&core,&l));finish();
 ready(640,480,4,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(0,0,1,-1,0,sizeof(stream),0,0);sim.unstable=0x2020;sim.unstable_reads=0;
 assert(i9100_mfc_poll(&core)==I9100_MFC_REPLY&&dec.raw_type==1);sim.unstable=0;finish();
 ready(640,480,4,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 sim.ns=core.started+500000001ULL;frame_response(0,0,1,-1,0,sizeof(stream),0,0);
 assert(i9100_mfc_poll(&core)==I9100_MFC_TIMEOUT&&core.last_reply==5);finish();
 ready(640,480,4,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(2,4,0,-1,-1,1,0,0);assert(i9100_mfc_poll(&core)==I9100_MFC_REPLY);finish();
 ready(640,480,4,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(0,0,1,-1,0,8,0,0);assert(!i9100_mfc_poll(&core));mem_a[core.plan.stream_offset+12]^=1;
 assert(i9100_mfc_decoder_frame(&core,0,0,0,0)==I9100_MFC_HASH&&core.first_error==I9100_MFC_HASH);finish();
 header(640,480,4,sizeof(stream));struct i9100_mfc_decode_io io={&sim,nc};assert(!i9100_mfc_decoder_bind(1,&core,&dec,&io));
 sim.drop=0x68c;sim.drop_count=1;assert(i9100_mfc_decoder_init_buffers(&core)==I9100_MFC_IO&&!core.bank_a);finish();
 ready(640,480,4,sizeof(stream));sim.regs[0x600/4]=0x123456U;assert(i9100_mfc_stop(&core)==I9100_MFC_OWNER&&core.bank_a&&core.decoder);
 sim.regs[0x600/4]=dec.regs[2].written; /* Restore exact owned chroma0 (scratch entries are first). */finish();
}
static void pressure_drain_stop(void)
{
 ready(128,96,1,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(1,0,1,0,0,sizeof(stream),1,1);assert(!i9100_mfc_poll(&core));
 struct i9100_mfc_frame_lease l={0};assert(!i9100_mfc_decoder_acquire(&core,&l));unsigned writes=sim.writes;
 assert(i9100_mfc_decoder_frame(&core,stream,sizeof(stream),2,0)==I9100_MFC_BUSY&&sim.writes==writes);
 assert(!i9100_mfc_decoder_release(&core,&l));finish();
 /* Real LAST can still finish decoding an internally retained picture;
  * use its prior input tag, while display type is independent. */
 ready(128,96,2,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,1));
 assert(sim.regs[0x2040/4]==0x20002);frame_response(0,4,0,-1,-1,sizeof(stream),0,0);assert(!i9100_mfc_poll(&core));
 assert(!i9100_mfc_decoder_frame(&core,0,0,0,1));frame_response(1,0,1,0,0,0,1,1);assert(!i9100_mfc_poll(&core));
 memset(&l,0,sizeof(l));assert(!i9100_mfc_decoder_acquire(&core,&l));assert(!i9100_mfc_decoder_release(&core,&l));
 assert(!i9100_mfc_decoder_frame(&core,0,0,0,1));frame_response(3,4,0,-1,-1,0,0,0);assert(!i9100_mfc_poll(&core)&&dec.drained);finish();
 ready(128,96,1,sizeof(stream));
 for(unsigned i=0;i<3;i++){assert(!i9100_mfc_decoder_frame(&core,0,0,0,1));frame_response(0,4,0,-1,-1,0,0,0);assert(!i9100_mfc_poll(&core));}
 assert(i9100_mfc_decoder_frame(&core,0,0,0,1)==I9100_MFC_TIMEOUT&&core.first_error==I9100_MFC_TIMEOUT);finish();
 ready(128,96,1,8);
 for(unsigned i=0;i<33;i++){assert(!i9100_mfc_decoder_frame(&core,0,0,i?0:1,0));frame_response(0,4,0,-1,-1,0,0,0);
  int e=i9100_mfc_poll(&core);assert(e==(i==32?I9100_MFC_TIMEOUT:0));}
 finish();
 ready(128,96,2,sizeof(stream));assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),1,0));
 frame_response(1,0,1,0,0,sizeof(stream),1,1);assert(!i9100_mfc_poll(&core));memset(&l,0,sizeof(l));assert(!i9100_mfc_decoder_acquire(&core,&l));
 assert(!i9100_mfc_decoder_frame(&core,stream,sizeof(stream),2,0));sim.reset_stuck=1;sim.regs[0x510/4]=3;
 sim.ns=core.started+500000001ULL;assert(i9100_mfc_poll(&core)==I9100_MFC_TIMEOUT&&banks[0].b.state==I9100_DMA_QUARANTINED);
 assert(!i9100_mfc_decoder_release(&core,&l));assert(i9100_mfc_stop(&core)!=0&&core.bank_a);sim.reset_stuck=0;finish();
}
int main(void)
{
 struct i9100_mfc_h264_plan p;struct i9100_mfc_h264_sizes s;struct i9100_mfc_dpb_layout l;
 assert(!i9100_mfc_h264_plan(0x80000,8U*1024U*1024U,sizeof(stream),&p));assert(!i9100_mfc_h264_sizes(640,480,4,&s));
 assert(i9100_mfc_dpb_layout(&p,&s,8U*1024U*1024U,1024U*1024U,&l)==I9100_MFC_LAYOUT);
 assert(!i9100_mfc_dpb_layout(&p,&s,8U*1024U*1024U,8U*1024U*1024U,&l));assert(l.end_b==1572864U);
 reorder_tail();tiling(128,96);tiling(256,128);tiling(640,480);tiling(642,482);tiling(1920,1080);failures();pressure_drain_stop();
 puts("PASS actual MFC core+DPB: mailbox routing, absolute surfaces, B-frame order, retained partial input, display-only, repeated LAST/EMPTY, exact leases, transactional NV12MT copy, capacity/NC/hash/address/reply/timeout/stop retention; explicit firmware+memory facades, hardware=false");return 0;
}
