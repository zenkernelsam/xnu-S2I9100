/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include "i9100_camera_protocol.h"
#include "i9100_mfc.h"
#include "i9100_video_layout.h"
#include "i9100_sysmmu.h"
struct model {
 unsigned locked,calls,fail_call,bad_length,mode_stuck,freeze;
 uint32_t category,reg,width,values[15][256];uint16_t page,address,front_id,revision;
 uint64_t ns;uint32_t mfc[0x3000/4],writes,reset_stuck,mailbox;
 unsigned drop_offset,drop_count,hash_fail,hash_calls,corrupt_hash,channel_command;
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
 struct model *p=c;assert(p->locked&&off<0x3000&&!(off&3U));p->writes++;
 if(p->drop_count&&p->drop_offset==off){p->drop_count--;return;}p->mfc[off/4]=value;
 if(off==0&&value!=0x3ff&&!p->reset_stuck)p->mfc[0x510/4]=0;
 if(off==0x30&&value){p->mailbox=value;p->mfc[0x30/4]=0;}
 if(off==0x2040&&value!=UINT32_MAX)p->channel_command=value;
}
static void barrier(void *c){(void)c;}
static int hash(void *cookie,const void *bytes,uint32_t length,uint8_t digest[32])
{struct model *p=cookie;assert(p->locked);p->hash_calls++;if(p->hash_fail==p->hash_calls)return -1;
 if(!SHA256(bytes,length,digest))return -1;if(p->corrupt_hash==p->hash_calls)digest[0]^=1U;return 0;}
static struct i9100_mfc_io mfc_io={&m,read,write,lock,unlock,now,delay,barrier,hash};
struct block {struct i9100_dma_buffer buffer;unsigned locked;};
static struct block a,b;
static uint8_t arena_a[8388608] __attribute__((aligned(64)));
static uint8_t arena_b[1048576] __attribute__((aligned(64)));
static uint8_t firmware[262144];
static const uint8_t stream[]={0,0,0,1,0x67,0x42,0,0x1e,0xf4,0x05,0x01,0xec,0x80};
static int block_lock(void *c){struct block *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void block_unlock(void *c){struct block *p=c;assert(p->locked);p->locked=0;}
static int synchronize(void *c,uintptr_t cpu,uint32_t physical,uint32_t bytes,unsigned action)
{struct block *p=c;assert(p->locked&&cpu==p->buffer.cpu&&physical==p->buffer.physical&&bytes==p->buffer.bytes&&action>=1&&action<=4);return 0;}
static int idle(void *c){(void)c;return i9100_mfc_idle(m.owner);}
static void free_block(void *c,uintptr_t cpu,uint32_t physical,uint32_t bytes){(void)c;(void)cpu;(void)physical;(void)bytes;}
static void bind(struct block *block,void *cpu,uint32_t physical)
{memset(block,0,sizeof(*block));struct i9100_dma_io io={block,block_lock,block_unlock,synchronize,idle,free_block};uint32_t bytes=cpu==arena_a?sizeof(arena_a):sizeof(arena_b);
 assert(!i9100_dma_bind(&block->buffer,&io,(uintptr_t)cpu,physical,bytes));assert(!i9100_dma_enable(&block->buffer,1));}
static struct i9100_mfc_firmware_profile profile(void)
{
 for(unsigned n=0;n<sizeof(firmware);n++)firmware[n]=(uint8_t)(n*13U+7U);
 struct i9100_mfc_firmware_profile p={0};p.board_data_reviewed=1;p.system_bytes=0x80000;
 p.firmware_bytes=sizeof(firmware);p.expected_version=0x211116;p.timeout_ms=500;
 assert(SHA256(firmware,sizeof(firmware),p.sha256));return p;
}
static void mfc_setup(struct i9100_mfc *f)
{
 memset(&m,0,sizeof(m));memset(f,0,sizeof(*f));assert(!i9100_mfc_init(f,&mfc_io));m.owner=f;
 bind(&a,arena_a,0x40000000);bind(&b,arena_b,0x41000000);
 assert(!m.writes);assert(!i9100_mfc_enable(f,1,1));
}
static void mfc_stage(struct i9100_mfc *f)
{
 mfc_setup(f);struct i9100_mfc_firmware_profile p=profile();
 assert(!i9100_mfc_stage_firmware(f,&a.buffer,&b.buffer,&p,firmware,sizeof(firmware)));
}
static void mfc_init(struct i9100_mfc *f)
{
 mfc_stage(f);
 assert(!i9100_mfc_stage_h264(f,stream,sizeof(stream)));assert(!i9100_mfc_boot(f));
 assert(a.buffer.state==I9100_DMA_DEVICE&&b.buffer.state==I9100_DMA_DEVICE&&m.mfc[0]==0x3ff);
}
static void reply(unsigned response,unsigned result,unsigned error)
{m.mfc[0x44/4]=response;m.mfc[0x48/4]=result;m.mfc[0x4c/4]=error;m.mfc[0x50/4]=m.mfc[0x54/4]=0;
 m.mfc[8/4]=1;m.mfc[0x58/4]=0x211116;}
static void mfc(void)
{
 struct i9100_mfc f;mfc_init(&f);assert(i9100_mfc_poll(&f)==I9100_MFC_BUSY);
 reply(9,0,0);assert(!i9100_mfc_poll(&f)&&f.state==1);
 assert(!i9100_mfc_sys_init(&f)&&m.mailbox==3&&m.mfc[0x34/4]==0x80000U);
 reply(8,0x60000,0);assert(!i9100_mfc_poll(&f)&&f.state==2);
 assert(!i9100_mfc_open_h264(&f));assert(m.mailbox==1&&m.mfc[0x3c/4]==256U&&m.mfc[0x40/4]==614400U);
 reply(1,2,0);assert(!i9100_mfc_poll(&f)&&f.instance_open&&f.instance==2);
 assert(!i9100_mfc_parse_h264_header(&f)&&m.channel_command==0x10002U);
 assert(m.mfc[0x2064/4]==f.plan.shared_offset&&m.mfc[0x2044/4]==f.plan.stream_offset>>11);
 m.mfc[0x2000/4]=2;m.mfc[0x2008/4]=640;m.mfc[0x2004/4]=480;m.mfc[0x200c/4]=4;m.mfc[0x2018/4]=8;
 reply(4,0,0);assert(!i9100_mfc_poll(&f)&&f.state==3&&f.sequence.luma_bytes==311296U&&f.sequence.dpb_count==4U);
 assert(!i9100_mfc_close(&f)&&m.mailbox==2&&m.mfc[0x34/4]==2);reply(2,0,0);assert(!i9100_mfc_poll(&f)&&!f.instance_open);
 assert(!i9100_mfc_stop(&f)&&!f.risc_running&&a.buffer.state==I9100_DMA_CPU&&b.buffer.state==I9100_DMA_CPU);
 mfc_init(&f);reply(8,0,0);assert(i9100_mfc_poll(&f)==I9100_MFC_REPLY&&!f.risc_running);
 mfc_init(&f);reply(32,0,123);assert(i9100_mfc_poll(&f)==I9100_MFC_FIRMWARE&&f.result[1]==123);
 mfc_init(&f);m.reset_stuck=1;m.mfc[0x510/4]=3;m.freeze=1;m.ns=f.started+500000001ULL;
 assert(i9100_mfc_poll(&f)==I9100_MFC_TIMEOUT&&f.fault&&f.risc_running);
 assert(a.buffer.state==I9100_DMA_QUARANTINED&&i9100_dma_release(&a.buffer)==I9100_DMA_BUSY);
 m.reset_stuck=0;assert(!i9100_mfc_stop(&f)&&a.buffer.state==I9100_DMA_CPU);
}
static void mfc_failures(void)
{
 struct i9100_mfc f;struct i9100_mfc_firmware_profile p;
 mfc_setup(&f);p=profile();p.sha256[0]^=1U;unsigned writes=m.writes;
 assert(i9100_mfc_stage_firmware(&f,&a.buffer,&b.buffer,&p,firmware,sizeof(firmware))==I9100_MFC_HASH);
 assert(!f.staged&&!a.buffer.holds&&!b.buffer.holds&&m.writes==writes);
 mfc_setup(&f);p=profile();a.buffer.physical+=8192U;
 assert(i9100_mfc_stage_firmware(&f,&a.buffer,&b.buffer,&p,firmware,sizeof(firmware))==I9100_MFC_ARGUMENT&&!m.writes);
 mfc_setup(&f);p=profile();p.board_data_reviewed=0;
 assert(i9100_mfc_stage_firmware(&f,&a.buffer,&b.buffer,&p,firmware,sizeof(firmware))==I9100_MFC_ARGUMENT);
 mfc_setup(&f);p=profile();m.corrupt_hash=2;
 assert(i9100_mfc_stage_firmware(&f,&a.buffer,&b.buffer,&p,firmware,sizeof(firmware))==I9100_MFC_HASH);
 assert(!f.staged&&!a.buffer.holds&&!b.buffer.holds&&!m.writes);
 mfc_stage(&f);arena_a[16]^=1U;
 assert(i9100_mfc_boot(&f)==I9100_MFC_HASH&&!f.risc_running&&!m.writes&&!a.buffer.holds&&!b.buffer.holds);
 mfc_stage(&f);assert(!i9100_mfc_stage_h264(&f,stream,sizeof(stream)));arena_a[f.plan.stream_offset]^=1U;
 assert(i9100_mfc_boot(&f)==I9100_MFC_HASH&&!m.writes&&!f.risc_running);
 mfc_stage(&f);m.drop_offset=0x508;m.drop_count=1;
 assert(i9100_mfc_boot(&f)==I9100_MFC_IO&&!f.risc_running&&a.buffer.state==I9100_DMA_CPU&&!a.buffer.holds);
 mfc_init(&f);reply(9,0,0);m.mfc[8/4]=0;
 assert(i9100_mfc_poll(&f)==I9100_MFC_BUSY&&f.pending&&!f.state);m.mfc[8/4]=1;assert(!i9100_mfc_poll(&f));
 assert(!i9100_mfc_sys_init(&f));reply(8,0x60000,0);m.ns=f.started+500000001ULL;
 assert(i9100_mfc_poll(&f)==I9100_MFC_TIMEOUT&&f.first_error==I9100_MFC_TIMEOUT&&!f.state&&f.last_reply==8U);
 assert(a.buffer.state==I9100_DMA_CPU&&!a.buffer.holds);
 mfc_init(&f);reply(9,0,0);assert(!i9100_mfc_poll(&f));assert(!i9100_mfc_sys_init(&f));
 reply(8,0x90000,0);assert(i9100_mfc_poll(&f)==I9100_MFC_FIRMWARE&&!f.state);
 mfc_init(&f);reply(9,0,0);assert(!i9100_mfc_poll(&f));assert(!i9100_mfc_sys_init(&f));
 reply(8,0x60000,0);m.mfc[0x58/4]^=1U;assert(i9100_mfc_poll(&f)==I9100_MFC_FIRMWARE);
 mfc_init(&f);reply(9,0,0);assert(!i9100_mfc_poll(&f));m.drop_offset=0x34;m.drop_count=1;
 assert(i9100_mfc_sys_init(&f)==I9100_MFC_IO&&!f.risc_running&&!f.pending);
 mfc_init(&f);reply(9,0,0);assert(!i9100_mfc_poll(&f));assert(!i9100_mfc_sys_init(&f));reply(8,0x60000,0);
 assert(!i9100_mfc_poll(&f));assert(!i9100_mfc_open_h264(&f));reply(1,2,0);assert(!i9100_mfc_poll(&f));
 assert(!i9100_mfc_parse_h264_header(&f));m.mfc[0x2000/4]=3;m.mfc[0x2008/4]=640;m.mfc[0x2004/4]=480;m.mfc[0x200c/4]=4;
 reply(4,0,0);assert(i9100_mfc_poll(&f)==I9100_MFC_REPLY&&!f.state&&!f.sequence.dpb_count);
 mfc_init(&f);m.freeze=1;for(unsigned n=0;n<10000;n++)assert(i9100_mfc_poll(&f)==I9100_MFC_BUSY);
 assert(i9100_mfc_poll(&f)==I9100_MFC_TIMEOUT&&!a.buffer.holds);
 mfc_stage(&f);struct i9100_mfc copied=f;assert(i9100_mfc_boot(&copied)==I9100_MFC_OWNER&&!m.writes);
 assert(!i9100_mfc_stop(&f));
 struct i9100_mfc_h264_plan layout={0};assert(!i9100_mfc_h264_plan(0x80000,8388608,sizeof(stream),&layout));
 assert(layout.context_offset==0x80000&&layout.context_bytes==0x96000&&layout.stream_capacity==0x400000&&
        !(layout.context_offset&2047U)&&!(layout.descriptor_offset&2047U)&&layout.shared_offset==0x116000U);
 assert(i9100_mfc_h264_plan(0x80000,1048576,sizeof(stream),&layout));
 struct i9100_mfc_h264_sizes sizes={0};assert(!i9100_mfc_h264_sizes(640,480,4,&sizes));
 assert(sizes.pitch==640U&&sizes.luma_bytes==311296U&&sizes.chroma_bytes==163840U&&sizes.mv_bytes==81920U);
 assert(i9100_mfc_h264_sizes(UINT32_MAX,480,4,&sizes)||sizes.width!=UINT32_MAX);
 assert(i9100_mfc_h264_sizes(1920,1080,16,&sizes)); /* The 16MiB private bank cap is real. */
 const char *destination=getenv("I9100_MFC_CORE_OUTPUT");
 if(destination){
  p=profile();assert(!i9100_mfc_h264_plan(p.system_bytes,sizeof(arena_a),sizeof(stream),&layout));
  FILE *file=fopen(destination,"w");assert(file);
  fprintf(file,"{\"model_fixture\":true,\"firmware_sha256\":\"");
  for(unsigned n=0;n<32;n++)fprintf(file,"%02x",p.sha256[n]);
  fprintf(file,"\",\"plan\":{\"system_bytes\":%u,\"context_offset\":%u,\"context_bytes\":%u,\"shared_offset\":%u,\"shared_bytes\":%u,"
   "\"descriptor_offset\":%u,\"descriptor_bytes\":%u,\"stream_offset\":%u,\"stream_capacity\":%u,\"stream_bytes\":%u,\"end_offset\":%u}}\n",
   layout.system_bytes,layout.context_offset,layout.context_bytes,layout.shared_offset,layout.shared_bytes,layout.descriptor_offset,layout.descriptor_bytes,
   layout.stream_offset,layout.stream_capacity,layout.stream_bytes,layout.end_offset);assert(!fclose(file));
 }
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
{camera();mfc();mfc_failures();video();sysmmu();puts("PASS actual MFCv5.1 source/landed/boot SHA, 128KiB base, fixed SYS_INIT reservation, 600KiB context, bounded real header mailbox, deadline/stale/drop/frozen/quarantine; camera wire, video planes and exact SYSMMU pages retained");return 0;}
