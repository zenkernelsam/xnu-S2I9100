/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_camera_protocol.h"
#include "i9100_mfc.h"
#include "i9100_video_layout.h"
#include "i9100_sysmmu.h"
struct model {
 unsigned locked,calls,fail_call,bad_length,mode_stuck,freeze;
 uint32_t category,reg,width,values[15][256];uint16_t page,address,front_id,revision;
 uint64_t ns;uint32_t mfc[0x3000/4],writes,reset_stuck,mailbox;
 struct i9100_mfc *owner;
};
static struct model m;
static int lock(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint64_t now(void *c){return ((struct model *)c)->ns;}
static int delay(void *c,uint32_t us){struct model *p=c;if(!p->freeze)p->ns+=us*1000ULL;return 0;}
static int transfer(void *c,const struct i9100_i2c_message *msg,unsigned count,unsigned *completed)
{
 struct model *p=c;assert(p->locked);*completed=0;if(++p->calls==p->fail_call)return -1;
 if(msg[0].address==0x1f){assert(count==1);
  if(msg[0].read){assert(msg[0].length==p->width+1U);msg[0].bytes[0]=(uint8_t)(p->width+1U+p->bad_length);
   uint32_t value=p->values[p->category][p->reg];for(unsigned n=0;n<p->width;n++)msg[0].bytes[n+1]=(uint8_t)(value>>(8U*(p->width-1U-n)));}
  else{assert(msg[0].length==5&&msg[0].bytes[0]==5);p->category=msg[0].bytes[2];p->reg=msg[0].bytes[3];
   if(msg[0].bytes[1]==1)p->width=msg[0].bytes[4];
   else{assert(msg[0].bytes[1]==2&&!p->category&&p->reg==0xb&&msg[0].bytes[4]==1);if(!p->mode_stuck)p->values[0][0xb]=1;}}
 }else{assert(msg[0].address==0x2d);
  if(count==1){assert(msg[0].length==4);uint16_t reg=(uint16_t)(((uint16_t)msg[0].bytes[0]<<8)|msg[0].bytes[1]);
   uint16_t value=(uint16_t)(((uint16_t)msg[0].bytes[2]<<8)|msg[0].bytes[3]);if(reg==0x2c)p->page=value;else{assert(reg==0x2e);p->address=value;}}
  else{assert(count==2&&msg[0].length==2&&msg[0].bytes[0]==0xf&&msg[0].bytes[1]==0x12&&msg[1].read&&msg[1].length==2);
   assert(p->page==0xd000);uint16_t value=p->address==0x1006?p->front_id:p->revision;msg[1].bytes[0]=(uint8_t)(value>>8);msg[1].bytes[1]=(uint8_t)value;}}
 *completed=count;return 0;
}
static struct i9100_peripheral_i2c camera_io={&m,transfer,lock,unlock,now,delay};
static void camera(void)
{
 memset(&m,0,sizeof(m));struct i9100_camera c={0};
 assert(!i9100_camera_init(&c,&camera_io,I9100_CAMERA_M5MO)&&!m.calls);
 assert(i9100_camera_enable(&c,1,0)==I9100_CAM_ARGUMENT&&!m.calls);
 m.values[0][1]=0x35;m.values[0][2]=0x1234;m.values[0][4]=0x1001;m.values[0][0xb]=2;
 assert(!i9100_camera_enable(&c,1,1));assert(c.project==0x35&&c.firmware==0x1234&&c.hardware==0x1001);
 uint32_t value=0;m.values[7][3]=0x12345678;assert(!i9100_m5mo_read(&c,7,3,4,&value)&&value==0x12345678);
 assert(!i9100_m5mo_set_parameter_mode(&c)&&m.values[0][0xb]==1);
 assert(i9100_m5mo_read(&c,15,0,1,&value)==I9100_CAM_ARGUMENT);
 m.bad_length=1;value=0xabcdef;assert(i9100_m5mo_read(&c,7,3,4,&value)==I9100_CAM_FRAME&&value==0xabcdef&&c.fault);
 memset(&c,0,sizeof(c));memset(&m,0,sizeof(m));m.front_id=0x05ba;m.revision=0xa0;
 assert(!i9100_camera_init(&c,&camera_io,I9100_CAMERA_S5K5BAFX));assert(!i9100_camera_enable(&c,1,1));
 uint16_t v=0;assert(!i9100_s5k5bafx_read(&c,0xd000,0x1006,&v)&&v==0x05ba);
 memset(&c,0,sizeof(c));m.front_id=0x05bb;assert(!i9100_camera_init(&c,&camera_io,I9100_CAMERA_S5K5BAFX));
 assert(i9100_camera_enable(&c,1,1)==I9100_CAM_ID&&!c.enabled);
}
static uint32_t read(void *c,unsigned off){struct model *p=c;assert(off<0x3000&&!(off&3U));return p->mfc[off/4];}
static void write(void *c,unsigned off,uint32_t value)
{
 struct model *p=c;assert(p->locked&&off<0x3000&&!(off&3U));p->writes++;p->mfc[off/4]=value;
 if(off==0&&value!=0x3ff&&!p->reset_stuck)p->mfc[0x510/4]=0;
 if(off==0x30&&value){p->mailbox=value;p->mfc[0x30/4]=0;}
}
static void barrier(void *c){(void)c;}
static struct i9100_mfc_io mfc_io={&m,read,write,lock,unlock,now,delay,barrier};
struct block {struct i9100_dma_buffer buffer;unsigned locked;};
static struct block a,b;
static uint8_t arena_a[1048576] __attribute__((aligned(64)));
static uint8_t arena_b[1048576] __attribute__((aligned(64)));
static int block_lock(void *c){struct block *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void block_unlock(void *c){struct block *p=c;assert(p->locked);p->locked=0;}
static int synchronize(void *c,uintptr_t cpu,uint32_t physical,uint32_t bytes,unsigned action)
{struct block *p=c;assert(p->locked&&cpu==p->buffer.cpu&&physical==p->buffer.physical&&bytes==p->buffer.bytes&&action>=1&&action<=4);return 0;}
static int idle(void *c){(void)c;return i9100_mfc_idle(m.owner);}
static void free_block(void *c,uintptr_t cpu,uint32_t physical,uint32_t bytes){(void)c;(void)cpu;(void)physical;(void)bytes;}
static void bind(struct block *block,void *cpu,uint32_t physical)
{memset(block,0,sizeof(*block));struct i9100_dma_io io={block,block_lock,block_unlock,synchronize,idle,free_block};assert(!i9100_dma_bind(&block->buffer,&io,(uintptr_t)cpu,physical,1048576));assert(!i9100_dma_enable(&block->buffer,1));}
static void mfc_init(struct i9100_mfc *f)
{
 memset(&m,0,sizeof(m));memset(f,0,sizeof(*f));assert(!i9100_mfc_init(f,&mfc_io));m.owner=f;
 bind(&a,arena_a,0x40000000);bind(&b,arena_b,0x41000000);
 assert(!m.writes);assert(!i9100_mfc_enable(f,1,1));assert(!i9100_mfc_boot(f,&a.buffer,&b.buffer,262144));
 assert(a.buffer.state==I9100_DMA_DEVICE&&b.buffer.state==I9100_DMA_DEVICE&&m.mfc[0]==0x3ff);
}
static void reply(unsigned response,unsigned result,unsigned error)
{m.mfc[0x44/4]=response;m.mfc[0x48/4]=result;m.mfc[0x4c/4]=error;m.mfc[0x58/4]=0x211116;}
static void mfc(void)
{
 struct i9100_mfc f;mfc_init(&f);assert(i9100_mfc_poll(&f)==I9100_MFC_BUSY);
 reply(9,0,0);assert(!i9100_mfc_poll(&f)&&f.state==1);
 assert(!i9100_mfc_sys_init(&f)&&m.mailbox==3&&m.mfc[0x34/4]==262144);
 reply(8,0,0);assert(!i9100_mfc_poll(&f)&&f.state==2);
 assert(i9100_mfc_open_h264(&f,0,131072)==I9100_MFC_ARGUMENT);
 assert(!i9100_mfc_open_h264(&f,262144,131072));assert(m.mailbox==1&&m.mfc[0x3c/4]==128U&&m.mfc[0x40/4]==131072);
 reply(1,2,0);assert(!i9100_mfc_poll(&f)&&f.instance_open&&f.instance==2);
 assert(!i9100_mfc_close(&f)&&m.mailbox==2&&m.mfc[0x34/4]==2);reply(2,0,0);assert(!i9100_mfc_poll(&f)&&!f.instance_open);
 assert(!i9100_mfc_stop(&f)&&!f.risc_running&&a.buffer.state==I9100_DMA_CPU&&b.buffer.state==I9100_DMA_CPU);
 mfc_init(&f);reply(8,0,0);assert(i9100_mfc_poll(&f)==I9100_MFC_REPLY&&!f.risc_running);
 mfc_init(&f);reply(32,0,123);assert(i9100_mfc_poll(&f)==I9100_MFC_FIRMWARE&&f.result[1]==123);
 mfc_init(&f);m.reset_stuck=1;m.mfc[0x510/4]=3;m.freeze=1;m.ns=f.started+500000001ULL;
 assert(i9100_mfc_poll(&f)==I9100_MFC_TIMEOUT&&f.fault&&f.risc_running);
 assert(a.buffer.state==I9100_DMA_QUARANTINED&&i9100_dma_release(&a.buffer)==I9100_DMA_OWNER);
 m.reset_stuck=0;assert(!i9100_mfc_stop(&f)&&a.buffer.state==I9100_DMA_CPU);
}
static void video(void)
{
 struct i9100_video_layout v;assert(!i9100_video_layout(I9100_VIDEO_NV12MT,1920,1080,&v));
 assert(v.pitch==1920&&!(v.plane_bytes[0]&8191U)&&!(v.plane_bytes[1]&8191U)&&v.total_bytes>=1920U*1080U*3U/2U);
 assert(!i9100_video_range(&v,1,0,v.plane_bytes[1],v.total_bytes));
 assert(i9100_video_range(&v,1,v.plane_bytes[1],1,v.total_bytes));
 assert(i9100_video_range(&v,1,0,v.plane_bytes[1],v.total_bytes-1));
 assert(i9100_video_layout(I9100_VIDEO_NV12MT,UINT32_MAX,1080,&v));
 assert(i9100_video_layout(I9100_VIDEO_NV12,641,480,&v));
}
struct mmu_model {uint32_t regs[2][32];unsigned locked,master_busy;struct i9100_sysmmu *owner;};
static struct mmu_model mm;
static struct block table_block;
static uint32_t table_words[8192] __attribute__((aligned(16384)));
static int mm_lock(void *c){struct mmu_model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void mm_unlock(void *c){struct mmu_model *p=c;assert(p->locked);p->locked=0;}
static uint32_t mm_read(void *c,unsigned unit,unsigned off){struct mmu_model *p=c;assert(unit<2&&off<128);return p->regs[unit][off/4];}
static void mm_write(void *c,unsigned unit,unsigned off,uint32_t value){struct mmu_model *p=c;assert(p->locked&&unit<2&&off<128);p->regs[unit][off/4]=value;}
static int masters_idle(void *c){return ((struct mmu_model *)c)->master_busy?-1:0;}
static int mm_idle(void *c){(void)c;return i9100_sysmmu_idle(mm.owner);}
static void sysmmu(void)
{
 struct i9100_mfc f;mfc_init(&f);assert(!i9100_mfc_stop(&f));
 memset(&mm,0,sizeof(mm));struct i9100_sysmmu s={0};mm.owner=&s;
 struct i9100_sysmmu_io io={&mm,mm_read,mm_write,mm_lock,mm_unlock,masters_idle,barrier};
 assert(!i9100_sysmmu_init(&s,&io,2));mm.regs[0][0x34/4]=mm.regs[1][0x34/4]=0x20000000;
 memset(&table_block,0,sizeof(table_block));struct i9100_dma_io dio={&table_block,block_lock,block_unlock,synchronize,mm_idle,free_block};
 assert(!i9100_dma_bind(&table_block.buffer,&dio,(uintptr_t)table_words,0x42000000,32768));assert(!i9100_dma_enable(&table_block.buffer,1));
 struct i9100_sysmmu_mapping map[2]={{&a.buffer,0,4096},{&b.buffer,0,8192}};
 assert(!i9100_sysmmu_build(&table_block.buffer,map,2));
 assert(table_words[0x400]==0x42004001&&table_words[0x410]==0x42004401);
 assert(table_words[4096]==0x40000002&&table_words[4352]==0x41000002&&table_words[4353]==0x41001002);
 assert(!i9100_sysmmu_enable(&s,1));table_words[4097]=0x40001002;
 assert(i9100_sysmmu_attach(&s,&table_block.buffer,map,2,1)==I9100_MMU_ARGUMENT);
 table_words[4097]=0;assert(!i9100_sysmmu_attach(&s,&table_block.buffer,map,2,1));
 assert(table_block.buffer.state==I9100_DMA_DEVICE&&a.buffer.holds==1&&b.buffer.holds==1);
 assert(i9100_dma_release(&a.buffer)==I9100_DMA_BUSY);assert(i9100_sysmmu_build(&table_block.buffer,map,2)==I9100_MMU_ARGUMENT);
 mm.master_busy=1;assert(i9100_sysmmu_detach(&s)==I9100_MMU_BUSY);
 mm.regs[1][0x18/4]=1;mm.regs[1][0x24/4]=0x43000000;
 assert(i9100_sysmmu_fault(&s)==I9100_MMU_FAULT&&s.fault_address[1]==0x43000000);
 assert(mm.regs[1][0]==5); /* no automatic unblock or missing-page grant */
 mm.master_busy=0;assert(!i9100_sysmmu_detach(&s)&&!a.buffer.holds&&!b.buffer.holds&&table_block.buffer.state==I9100_DMA_CPU);
}
int main(void)
{camera();mfc();video();sysmmu();puts("PASS separate camera wire/ID/length; MFCv5 sysinit8/context units/MC timeout/quarantine; video plane bounds; exact owned SYSMMU pages/pins/attach/fault/detach");return 0;}
