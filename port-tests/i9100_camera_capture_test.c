/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_csis.h"
#include "i9100_fimc.h"
struct hw {
 uint32_t regs[0x400/4],saved[0x400/4];
 unsigned locked,writes,reads,drop_offset,drop_once,reset_stuck,shadow_stuck,stop_stuck,freeze;
 unsigned sync_fail,syncs,frame_writes,early_takeback,no_lastend;
 uint64_t ns;
 struct i9100_fimc *engine;
 int csis;
};
static struct hw cs,fc;
static uint8_t pixels[618496] __attribute__((aligned(64)));
struct dma_model {struct i9100_dma_buffer buffer;unsigned locked;struct hw *hw;};
static struct dma_model dm;
static int lock(void *v){struct hw *h=v;if(h->locked)return -1;h->locked=1;return 0;}
static void unlock(void *v){struct hw *h=v;assert(h->locked);h->locked=0;}
static uint32_t read32(void *v,unsigned off)
{struct hw *h=v;assert(off<sizeof(h->regs)&&!(off&3U));h->reads++;return h->regs[off/4];}
static void write32(void *v,unsigned off,uint32_t value)
{
 struct hw *h=v;assert(h->locked&&off<sizeof(h->regs)&&!(off&3U));h->writes++;
 if(h->drop_once&&off==h->drop_offset){h->drop_once--;return;}
 if(h->csis&&off==0x14){h->regs[off/4]&=~value;return;}
 if(!h->csis&&off==8&&(value&(1U<<19))){value&=~(1U<<19);h->regs[0x64/4]&=~(1U<<17);}
 h->regs[off/4]=value;
 if(h->csis&&off==0){
  if(!h->reset_stuck)h->regs[0]&=~(1U<<4);
  if(!h->shadow_stuck)h->regs[0]&=~(1U<<16);
 }
 if(!h->csis&&off==0xc0){
  if(value&(1U<<31))h->regs[0x64/4]|=1U<<22;
  else if(!h->stop_stuck){
   if((h->regs[0x64/4]&(1U<<17))&&!h->no_lastend)h->regs[0x64/4]|=1U<<16;
   h->regs[0x64/4]&=~((1U<<26)|(1U<<22)|(1U<<21));
  }
 }
}
static void barrier(void *v){(void)v;}
static uint64_t now(void *v){return ((struct hw *)v)->ns;}
static int delay(void *v,uint32_t us){struct hw *h=v;if(!h->freeze)h->ns+=us*1000ULL;return 0;}
static struct i9100_camera_engine_io engine_io(struct hw *h)
{struct i9100_camera_engine_io io={h,read32,write32,lock,unlock,barrier,now,delay};return io;}
static int dma_lock(void *v){struct dma_model *d=v;if(d->locked)return -1;d->locked=1;return 0;}
static void dma_unlock(void *v){struct dma_model *d=v;assert(d->locked);d->locked=0;}
static int dma_sync(void *v,uintptr_t cpu,uint32_t physical,uint32_t bytes,unsigned action)
{
 struct dma_model *d=v;assert(d->locked&&cpu==(uintptr_t)pixels&&physical==0x46000000U&&bytes==sizeof(pixels));
 if(action==I9100_DMA_INVALIDATE&&i9100_fimc_idle(d->hw->engine))d->hw->early_takeback++;
 return ++d->hw->syncs==d->hw->sync_fail?-1:0;
}
static int dma_idle(void *v){struct dma_model *d=v;return i9100_fimc_idle(d->hw->engine);}
static void dma_release(void *v,uintptr_t cpu,uint32_t pa,uint32_t bytes)
{(void)v;(void)cpu;(void)pa;(void)bytes;}
static void setup_csis(struct i9100_csis *c)
{
 memset(&cs,0,sizeof(cs));cs.csis=1;
 cs.regs[0]=(1U<<31)|(1U<<7);cs.regs[1]=(1U<<6);cs.regs[2]=1U<<12;cs.regs[0x10/4]=1U<<7;
 memcpy(cs.saved,cs.regs,sizeof(cs.regs));memset(c,0,sizeof(*c));struct i9100_camera_engine_io io=engine_io(&cs);
 assert(!i9100_csis_init(c,&io)&&!cs.writes);
}
static struct i9100_csis_config camera_config={640,480,2,12,32,1,0x1e};
static void csis_tests(void)
{
 struct i9100_csis c;setup_csis(&c);
 assert(i9100_csis_start(&c,&camera_config)==I9100_CE_DISABLED&&!cs.writes);
 assert(i9100_csis_enable(&c,1,0)==I9100_CE_ARGUMENT&&!cs.writes);
 assert(!i9100_csis_enable(&c,1,1));assert(!i9100_csis_start(&c,&camera_config));
 assert(c.running&&cs.regs[0]&1U&&(cs.regs[1]&0xf800001fU)==(12U<<27|31U));
 assert((cs.regs[2]&255U)==0x79U&&cs.regs[0x2c/4]==0x028001e0U&&cs.regs[0x10/4]==(1U<<7));
 cs.regs[0x14/4]=(1U<<30)|(1U<<8);assert(!i9100_csis_poll(&c));
 assert(c.non_image_packets==1&&!c.errors&&cs.regs[0x14/4]==(1U<<8));
 cs.regs[0x14/4]|=1U<<1;assert(i9100_csis_poll(&c)==I9100_CE_FRAME);
 assert(c.errors==2U&&!c.running&&c.first_error==I9100_CE_FRAME&&!c.cleanup_error);
 assert(!i9100_csis_enable(&c,0,0));
 for(unsigned n=0;n<5;n++)assert((cs.regs[c.regs[n].offset/4]&c.regs[n].mask)==(cs.saved[c.regs[n].offset/4]&c.regs[n].mask));
 assert(cs.regs[0]&1U<<7&&cs.regs[1]&1U<<6&&cs.regs[2]&1U<<12);
 setup_csis(&c);cs.regs[0]|=1U;assert(i9100_csis_enable(&c,1,1)==I9100_CE_BUSY&&!cs.writes);
 setup_csis(&c);assert(!i9100_csis_enable(&c,1,1));cs.reset_stuck=cs.freeze=1;
 assert(i9100_csis_start(&c,&camera_config)==I9100_CE_TIMEOUT&&c.owned&&c.cleanup_error);
 cs.reset_stuck=0;cs.regs[0]&=~(1U<<4);assert(!i9100_csis_enable(&c,0,0));
 setup_csis(&c);assert(!i9100_csis_enable(&c,1,1));cs.shadow_stuck=cs.freeze=1;
 assert(i9100_csis_start(&c,&camera_config)==I9100_CE_TIMEOUT&&c.owned&&c.cleanup_error);
 cs.shadow_stuck=0;cs.regs[0]&=~(1U<<16);assert(!i9100_csis_enable(&c,0,0));
 setup_csis(&c);assert(!i9100_csis_enable(&c,1,1));cs.drop_offset=0x2c;cs.drop_once=1;
 assert(i9100_csis_start(&c,&camera_config)==I9100_CE_IO&&!c.running);assert(!i9100_csis_enable(&c,0,0));
 setup_csis(&c);assert(!i9100_csis_enable(&c,1,1));assert(!i9100_csis_start(&c,&camera_config));
 cs.regs[2]^=4U;assert(i9100_csis_enable(&c,0,0)==I9100_CE_OWNER&&c.owned);
 cs.regs[2]^=4U;assert(!i9100_csis_enable(&c,0,0));
 struct i9100_csis copy=c;assert(i9100_csis_stop(&copy)==I9100_CE_OWNER);
 setup_csis(&c);cs.regs[1]=UINT32_MAX;assert(i9100_csis_enable(&c,1,1)==I9100_CE_IO&&!cs.writes);
}
static struct i9100_fimc_config frame_config={640,480,0,2,32,1,500};
static void setup_fimc(struct i9100_fimc *c)
{
 memset(&fc,0,sizeof(fc));fc.engine=c;fc.ns=1000000ULL;
 fc.regs[0]=(1U<<28);fc.regs[8/4]=(1U<<30);fc.regs[0x188/4]=1U<<20;fc.regs[0x18c/4]=1U<<29;
 memcpy(fc.saved,fc.regs,sizeof(fc.regs));memset(c,0,sizeof(*c));struct i9100_camera_engine_io io=engine_io(&fc);
 assert(!i9100_fimc_init(c,&io)&&!fc.writes);memset(&dm,0,sizeof(dm));dm.hw=&fc;
 struct i9100_dma_io dio={&dm,dma_lock,dma_unlock,dma_sync,dma_idle,dma_release};
 assert(!i9100_dma_bind(&dm.buffer,&dio,(uintptr_t)pixels,0x46000000U,sizeof(pixels)));
 assert(!i9100_dma_enable(&dm.buffer,1));memset(pixels,0xcc,sizeof(pixels));
}
static void arrived(unsigned before)
{
 assert(fc.regs[0xc0/4]&(1U<<31));assert(fc.regs[0x1fc/4]==1U&&fc.regs[0x18/4]==dm.buffer.physical);
 assert(dm.buffer.state==I9100_DMA_DEVICE);
 for(unsigned n=0;n<640U*480U*2U;n++)pixels[n]=(uint8_t)(n*17U+3U); /* Explicit sensor/FIMC DMA facade. */
 fc.frame_writes++;fc.regs[0x64/4]|=1U<<17;fc.regs[0x68/4]=before<<7;
}
static void finish(struct i9100_fimc *c)
{
 assert(!i9100_fimc_enable(c,0,0));assert(!c->owned&&!c->buffer&&!c->consumer&&!c->held);
 assert(dm.buffer.state==I9100_DMA_CPU&&!dm.buffer.holds&&!fc.early_takeback);
 for(unsigned n=0;n<24U;n++)assert((fc.regs[c->regs[n].offset/4]&c->regs[n].mask)==(fc.saved[c->regs[n].offset/4]&c->regs[n].mask));
 assert(fc.regs[8/4]&(1U<<30)&&fc.regs[0x188/4]&(1U<<20)&&fc.regs[0x18c/4]&(1U<<29));
}
static void fimc_tests(void)
{
 struct i9100_fimc c;setup_fimc(&c);assert(i9100_fimc_capture(&c,&frame_config,&dm.buffer)==I9100_CE_DISABLED&&!fc.writes);
 assert(i9100_fimc_enable(&c,1,0)==I9100_CE_ARGUMENT&&!fc.writes);assert(!i9100_fimc_enable(&c,1,1));
 /* A leftover completion must be cleared before the actual arm. */
 fc.regs[0x64/4]|=(1U<<17)|(1U<<16);int result=i9100_fimc_capture(&c,&frame_config,&dm.buffer);
 if(result)fprintf(stderr,"capture=%d first=%d cleanup=%d status=%x cap=%x global=%x scaler=%x input=%x seq=%x bufferstate=%u syncs=%u\n",
  result,c.first_error,c.cleanup_error,fc.regs[0x64/4],fc.regs[0xc0/4],fc.regs[8/4],fc.regs[0x58/4],fc.regs[0xfc/4],fc.regs[0x1fc/4],dm.buffer.state,fc.syncs);
 assert(!result);
 assert(dm.buffer.state==I9100_DMA_DEVICE&&dm.buffer.holds==1&&c.running&&fc.regs[0x194/4]==0x11eU);
 assert(fc.regs[0x48/4]==((2U<<29)|(640U<<16)|480U)&&fc.regs[0x4c/4]==((1U<<30)|3U));
 assert(i9100_fimc_poll(&c)==I9100_CE_BUSY&&!c.ready&&!c.frame_observed&&!fc.frame_writes);
 struct i9100_camera_frame frame={0};assert(i9100_fimc_take_frame(&c,&frame)==I9100_CE_BUSY);
 arrived(1);assert(!i9100_fimc_poll(&c));assert(c.ready&&!c.running&&dm.buffer.state==I9100_DMA_CPU&&!fc.early_takeback);
 assert(!i9100_fimc_take_frame(&c,&frame)&&frame.bytes==614400U&&frame.buffer==&dm.buffer&&frame.status&(1U<<17));
 for(unsigned n=0;n<614400U;n++)assert(pixels[n]==(uint8_t)(n*17U+3U));
 for(unsigned n=614400U;n<sizeof(pixels);n++)assert(pixels[n]==0xccU);
 assert(i9100_dma_release(&dm.buffer)==I9100_DMA_BUSY);
 assert(i9100_fimc_stop(&c)==I9100_CE_BUSY&&c.consumer==&frame);
 struct i9100_camera_frame copied=frame;assert(i9100_fimc_release_frame(&c,&copied)==I9100_CE_OWNER);
 assert(!i9100_fimc_release_frame(&c,&frame)&&!frame.self&&!dm.buffer.holds);
 assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));arrived(1);assert(!i9100_fimc_poll(&c));
 assert(!i9100_fimc_take_frame(&c,&frame)&&frame.ticket==2U);assert(!i9100_fimc_release_frame(&c,&frame));finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));
 arrived(2);assert(i9100_fimc_poll(&c)==I9100_CE_FRAME&&!c.ready&&dm.buffer.state==I9100_DMA_CPU);finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));
 fc.no_lastend=1;arrived(1);assert(i9100_fimc_poll(&c)==I9100_CE_FRAME&&!c.ready);finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));
 fc.regs[0x64/4]|=1U<<31;assert(i9100_fimc_poll(&c)==I9100_CE_FRAME&&!c.ready);finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));fc.drop_offset=0x18;fc.drop_once=1;
 assert(i9100_fimc_capture(&c,&frame_config,&dm.buffer)==I9100_CE_IO&&!c.running&&!c.held);finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));
 fc.stop_stuck=fc.freeze=1;fc.ns=c.started+500000001ULL;
 assert(i9100_fimc_poll(&c)==I9100_CE_TIMEOUT&&c.owned&&c.held&&c.buffer==&dm.buffer);
 assert(dm.buffer.state==I9100_DMA_QUARANTINED&&dm.buffer.holds==1&&i9100_dma_release(&dm.buffer)==I9100_DMA_BUSY);
 fc.stop_stuck=0;assert(!i9100_fimc_stop(&c)&&!c.held&&dm.buffer.state==I9100_DMA_CPU);finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));fc.sync_fail=fc.syncs+1U;
 assert(i9100_fimc_capture(&c,&frame_config,&dm.buffer)==I9100_CE_IO&&!c.ready);fc.sync_fail=0;finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));arrived(1);
 fc.drop_offset=0xc0;fc.drop_once=2;assert(i9100_fimc_poll(&c)==I9100_CE_IO&&c.held&&dm.buffer.state==I9100_DMA_QUARANTINED);
 assert(!i9100_fimc_stop(&c));finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));
 fc.regs[0x194/4]^=4U;assert(i9100_fimc_enable(&c,0,0)==I9100_CE_OWNER&&c.owned);
 fc.regs[0x194/4]^=4U;finish(&c);
 setup_fimc(&c);fc.regs[0x1fc/4]=2;assert(i9100_fimc_enable(&c,1,1)==I9100_CE_BUSY&&!fc.writes);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));struct i9100_fimc_config bad=frame_config;bad.width=638;
 assert(i9100_fimc_capture(&c,&bad,&dm.buffer)==I9100_CE_ARGUMENT&&!fc.writes);finish(&c);
 setup_fimc(&c);assert(!i9100_fimc_enable(&c,1,1));assert(!i9100_fimc_capture(&c,&frame_config,&dm.buffer));fc.freeze=1;
 for(unsigned n=0;n<10000U;n++)assert(i9100_fimc_poll(&c)==I9100_CE_BUSY);
 assert(i9100_fimc_poll(&c)==I9100_CE_TIMEOUT);finish(&c);
}
int main(void)
{
 csis_tests();fimc_tests();
 puts("PASS real CSIS/FIMC cores with explicit register/sensor-DMA facade: observed owned errors, fresh frame/slot1, YUYV bytes, no early takeback, allocation pinning, loss/stuck/frozen/foreign-state cleanup");
 return 0;
}
