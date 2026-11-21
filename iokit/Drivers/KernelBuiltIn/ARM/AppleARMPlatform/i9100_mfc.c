/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc.h"
#include "i9100_mfc_decode.h"
#include <string.h>
enum {RESET=0,HOST_INTERRUPT=8,H2R=0x30,H2ARG=0x34,R2H=0x44,R2ARG=0x48,FW_VERSION=0x58,
 BANK_A=0x508,BANK_B=0x50c,MC_STATUS=0x510,STATE_FW=1,STATE_INITIALIZED=2,STATE_HEADER=3};
static uint32_t rd(struct i9100_mfc *m,unsigned off){return m->io.read32(m->io.cookie,off);}
static void wr(struct i9100_mfc *m,unsigned off,uint32_t value){m->io.write32(m->io.cookie,off,value);m->io.barrier(m->io.cookie);}
static int exact(struct i9100_mfc *m,unsigned off,uint32_t value)
{wr(m,off,value);return rd(m,off)!=value?I9100_MFC_IO:0;}
static int valid(struct i9100_mfc *m)
{return !m||!m->initialized?I9100_MFC_ARGUMENT:m->self!=(uintptr_t)m?I9100_MFC_OWNER:0;}
static int note(struct i9100_mfc *m,int e){if(e&&!m->first_error)m->first_error=e;return e;}
static int mc_idle(struct i9100_mfc *m)
{
 uint64_t start=m->io.now_ns(m->io.cookie);
 for(unsigned n=0;n<10000U;n++){
  uint32_t status=rd(m,MC_STATUS);if(status==UINT32_MAX)return I9100_MFC_IO;
  if(!(status&3U))return 0;
  uint64_t now=m->io.now_ns(m->io.cookie);if(now<start||now-start>=100000000ULL)break;
  if(m->io.delay_us(m->io.cookie,10))return I9100_MFC_IO;
 }return I9100_MFC_TIMEOUT;
}
int i9100_mfc_idle(void *cookie)
{
 struct i9100_mfc *m=cookie;if(valid(m)||m->risc_running)return -1;
 uint32_t rst=rd(m,RESET),status=rd(m,MC_STATUS);
 return rst==UINT32_MAX||status==UINT32_MAX||(rst&1U)||(status&3U)?-1:0;
}
static int restore(struct i9100_mfc *m,unsigned off,uint32_t saved,uint32_t written)
{
 uint32_t v=rd(m,off);
 /* Held RISC/full reset may have cleared this owned register to zero.
  * No other nonzero address/channel is overwritten as presumed ours. */
 if(v!=saved&&v!=written&&v)return I9100_MFC_OWNER;
 return exact(m,off,saved);
}
static int stop_locked(struct i9100_mfc *m)
{
 if(!m->staged&&!m->boot_attempted&&!m->bank_a)return 0;
 int e=0;
 if(m->boot_attempted){
  e=exact(m,RESET,0x3f6);if(!e)e=exact(m,RESET,0x3e2);
  if(!e)e=m->io.delay_us(m->io.cookie,10000)?I9100_MFC_IO:mc_idle(m);
  if(!e)e=exact(m,RESET,0);if(!e)m->risc_running=0;
 }
 /* Reset stops new DMA, but a displayed surface still pins both arenas.
  * Do not abort/unhold its backing allocations before the exact lease is
  * returned. Cleanup errors retain the decoder and its release authority. */
 if(!e&&m->decoder)e=i9100_mfc_decoder_cleanup_locked(m);
 if(e&&m->decoder){
  if(m->risc_running){if(m->bank_a)i9100_dma_quarantine(m->bank_a);if(m->bank_b)i9100_dma_quarantine(m->bank_b);}
  m->cleanup_error=e;m->fault=1;return e;
 }
 struct i9100_dma_buffer *buffers[2]={m->bank_a,m->bank_b};
 for(unsigned n=0;n<2;n++)if((m->held_mask&(1U<<n))&&buffers[n]&&
     (buffers[n]->state==I9100_DMA_DEVICE||buffers[n]->state==I9100_DMA_QUARANTINED)){
  int d=i9100_dma_abort(buffers[n]);if(!d)m->prepared_mask&=~(1U<<n);else if(!e)e=I9100_MFC_IO;
 }
 if(!e&&i9100_mfc_idle(m))e=I9100_MFC_BUSY;
 if(!e&&m->boot_attempted){e=exact(m,H2R,0);if(!e)e=exact(m,R2H,0);if(!e)e=exact(m,HOST_INTERRUPT,0);}
 if(!e&&(m->banks_written&1U))e=restore(m,BANK_A,m->initial_bank_a,m->bank_a->physical);
 if(!e&&(m->banks_written&2U))e=restore(m,BANK_B,m->initial_bank_b,m->bank_b->physical);
 if(!e&&(m->channels_written&1U))e=restore(m,0x2040,m->initial_channel[0],m->last_channel0);
 if(!e&&(m->channels_written&2U))e=restore(m,0x2080,m->initial_channel[1],UINT32_MAX);
 for(unsigned n=0;n<2&&!e;n++)if(m->held_mask&(1U<<n)){
  if(i9100_dma_unhold(buffers[n]))e=I9100_MFC_IO;else m->held_mask&=~(1U<<n);
 }
 m->cleanup_error=e;
 if(e){m->fault=1;return e;}
 m->bank_a=m->bank_b=0;m->staged=m->h264_staged=m->boot_attempted=m->prepared_mask=0;
 m->pending=m->state=m->instance_open=m->fault=m->banks_written=m->channels_written=0;return 0;
}
static int failed(struct i9100_mfc *m,int e)
{note(m,e);m->fault=1;m->cleanup_error=stop_locked(m);m->fault=1;return e;}
int i9100_mfc_fail_locked(struct i9100_mfc *m,int e){return failed(m,e);}
int i9100_mfc_init(struct i9100_mfc *m,const struct i9100_mfc_io *io)
{
 if(!m||m->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||!io->now_ns||!io->delay_us||!io->barrier)
  return I9100_MFC_ARGUMENT;
 memset(m,0,sizeof(*m));m->io=*io;m->self=(uintptr_t)m;m->initialized=1;return 0;
}
int i9100_mfc_enable(struct i9100_mfc *m,int enabled,int verified)
{
 int e=valid(m);if(e)return e;if(enabled!=0&&enabled!=1)return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 if(enabled){
  if(verified!=1){e=I9100_MFC_ARGUMENT;goto done;}
  if(m->enabled||m->staged||m->bank_a){e=I9100_MFC_BUSY;goto done;}
  if(i9100_mfc_idle(m)||rd(m,H2R)||rd(m,R2H)||rd(m,HOST_INTERRUPT)){e=I9100_MFC_BUSY;goto done;}
  m->initial_bank_a=rd(m,BANK_A);m->initial_bank_b=rd(m,BANK_B);
  m->initial_channel[0]=rd(m,0x2040);m->initial_channel[1]=rd(m,0x2080);
  if(m->initial_bank_a==UINT32_MAX||m->initial_bank_b==UINT32_MAX){e=I9100_MFC_IO;goto done;}
  m->enabled=1;m->fault=0;m->first_error=m->cleanup_error=0;
 }else{e=stop_locked(m);if(!e)m->enabled=m->fault=0;}
done:m->io.unlock(m->io.cookie);return e;
}
static int arena(struct i9100_dma_buffer *b)
{return b&&b->self==(uintptr_t)b&&b->state==I9100_DMA_CPU&&!(b->physical&(I9100_MFC_BASE_ALIGNMENT-1U))&&b->bytes>=1024U*1024U;}
static int overlaps(uintptr_t p,uint32_t bytes,uintptr_t q,uint32_t n)
{return p>UINTPTR_MAX-bytes||q>UINTPTR_MAX-n||(p<q+n&&q<p+bytes);}
int i9100_mfc_stage_firmware(struct i9100_mfc *m,struct i9100_dma_buffer *a,struct i9100_dma_buffer *b,
 const struct i9100_mfc_firmware_profile *profile,const void *image,uint32_t bytes)
{
 int e=valid(m);if(e)return e;
 if(!profile||profile->board_data_reviewed!=1U||(profile->system_bytes!=0x60000U&&profile->system_bytes!=0x80000U)||
    !image||!bytes||bytes!=profile->firmware_bytes||bytes>profile->system_bytes||!profile->expected_version||
    profile->expected_version==UINT32_MAX||profile->timeout_ms<10U||profile->timeout_ms>5000U||!m->io.sha256||
    !arena(a)||!arena(b)||(a->physical<(uint64_t)b->physical+b->bytes&&b->physical<(uint64_t)a->physical+a->bytes)||
    overlaps(a->cpu,a->bytes,b->cpu,b->bytes)||overlaps((uintptr_t)image,bytes,a->cpu,a->bytes)||
    overlaps((uintptr_t)image,bytes,b->cpu,b->bytes))return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 if(!m->enabled){e=I9100_MFC_DISABLED;goto done;}
 if(m->staged||m->bank_a||m->risc_running||m->fault||m->generation==UINT32_MAX){e=I9100_MFC_BUSY;goto done;}
 if(i9100_mfc_idle(m)){e=I9100_MFC_BUSY;goto done;}
 uint8_t digest[32];memset(digest,0,sizeof(digest));
 if(m->io.sha256(m->io.cookie,image,bytes,digest)){e=I9100_MFC_HASH;goto done;}
 if(memcmp(digest,profile->sha256,32)){e=I9100_MFC_HASH;goto done;}
 if(i9100_dma_hold(a)){e=I9100_MFC_OWNER;goto done;}m->bank_a=a;m->bank_b=b;m->held_mask=1;m->staged=1;
 if(i9100_dma_hold(b)){e=failed(m,I9100_MFC_OWNER);goto done;}m->held_mask=3;
 memset((void *)a->cpu,0,a->bytes);memset((void *)b->cpu,0,b->bytes);memcpy((void *)a->cpu,image,bytes);
 if(m->io.sha256(m->io.cookie,(const void *)a->cpu,bytes,digest)||memcmp(digest,profile->sha256,32)){
  e=failed(m,I9100_MFC_HASH);goto done;
 }
 m->profile=*profile;m->firmware_bytes=bytes;m->generation++;m->first_error=m->cleanup_error=0;
 m->firmware_version=m->sys_memory_bytes=m->consumed_bytes=0;memset(&m->plan,0,sizeof(m->plan));
 memset(&m->sequence,0,sizeof(m->sequence));
done:m->io.unlock(m->io.cookie);return e;
}
static int annexb(const uint8_t *data,uint32_t bytes)
{return bytes>=5U&&data[0]==0U&&data[1]==0U&&(data[2]==1U||(data[2]==0U&&data[3]==1U));}
int i9100_mfc_stage_h264(struct i9100_mfc *m,const void *data,uint32_t bytes)
{
 int e=valid(m);if(e)return e;
 if(!data||!bytes||bytes>I9100_MFC_CPB_BYTES||!annexb(data,bytes))return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 if(!m->enabled){e=I9100_MFC_DISABLED;goto done;}
 if(!m->staged||m->h264_staged||m->boot_attempted||m->fault||m->held_mask!=3U||
    m->bank_a->state!=I9100_DMA_CPU||m->bank_b->state!=I9100_DMA_CPU){e=I9100_MFC_BUSY;goto done;}
 if(overlaps((uintptr_t)data,bytes,m->bank_a->cpu,m->bank_a->bytes)||overlaps((uintptr_t)data,bytes,m->bank_b->cpu,m->bank_b->bytes))
  {e=I9100_MFC_ARGUMENT;goto done;}
 struct i9100_mfc_h264_plan plan;
 if(i9100_mfc_h264_plan(m->profile.system_bytes,m->bank_a->bytes,bytes,&plan)){e=I9100_MFC_LAYOUT;goto done;}
 uint8_t digest[32];
 if(m->io.sha256(m->io.cookie,data,bytes,digest)){e=I9100_MFC_HASH;goto done;}
 memcpy((void *)(m->bank_a->cpu+plan.stream_offset),data,bytes);
 if(m->io.sha256(m->io.cookie,(const void *)(m->bank_a->cpu+plan.stream_offset),bytes,m->stream_sha256)||
    memcmp(digest,m->stream_sha256,32)){e=failed(m,I9100_MFC_HASH);goto done;}
 m->plan=plan;m->h264_staged=1;
done:m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_boot(struct i9100_mfc *m)
{
 int e=valid(m);if(e)return e;if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 if(!m->enabled){e=I9100_MFC_DISABLED;goto done;}
 if(!m->staged||m->fault||m->risc_running||m->boot_attempted||m->held_mask!=3U){e=I9100_MFC_BUSY;goto done;}
 if(!arena(m->bank_a)||!arena(m->bank_b)){e=I9100_MFC_OWNER;goto done;}
 uint8_t digest[32];
 if(m->io.sha256(m->io.cookie,(const void *)m->bank_a->cpu,m->firmware_bytes,digest)||memcmp(digest,m->profile.sha256,32))
  {e=failed(m,I9100_MFC_HASH);goto done;}
 if(m->h264_staged&&(m->io.sha256(m->io.cookie,(const void *)(m->bank_a->cpu+m->plan.stream_offset),m->plan.stream_bytes,digest)||
    memcmp(digest,m->stream_sha256,32))){e=failed(m,I9100_MFC_HASH);goto done;}
 m->boot_attempted=1;e=exact(m,RESET,0x3f6);if(!e)e=exact(m,RESET,0x3e2);
 if(!e)e=m->io.delay_us(m->io.cookie,10000)?I9100_MFC_IO:mc_idle(m);
 if(!e)e=exact(m,RESET,0);if(!e)e=exact(m,RESET,0x3fe);if(e)goto fail;
 m->banks_written|=1U;e=exact(m,BANK_A,m->bank_a->physical);
 if(!e){m->banks_written|=2U;e=exact(m,BANK_B,m->bank_b->physical);}if(e)goto fail;
 m->channels_written=3;m->last_channel0=UINT32_MAX;
 e=exact(m,0x2040,UINT32_MAX);if(!e)e=exact(m,0x2080,UINT32_MAX);
 if(!e)e=exact(m,R2H,0);if(!e)e=exact(m,H2R,0);if(!e)e=exact(m,HOST_INTERRUPT,0);if(!e)e=exact(m,FW_VERSION,0);if(e)goto fail;
 e=i9100_dma_prepare(m->bank_a,I9100_DMA_BIDIRECTIONAL);if(!e){m->prepared_mask=1;e=i9100_dma_prepare(m->bank_b,I9100_DMA_BIDIRECTIONAL);}
 if(e){e=I9100_MFC_IO;goto fail;}m->prepared_mask=3;
 m->risc_running=m->pending=1;m->expected=9;m->started=m->io.now_ns(m->io.cookie);m->polls=0;
 e=exact(m,RESET,0x3ff);if(e)goto fail;goto done;
fail:e=failed(m,e);
done:m->io.unlock(m->io.cookie);return e;
}
static int request_ready(struct i9100_mfc *m)
{
 if(!m->enabled||!m->risc_running)return I9100_MFC_DISABLED;
 if(m->fault||m->pending||rd(m,H2R)||rd(m,R2H)||rd(m,HOST_INTERRUPT))return I9100_MFC_BUSY;
 if(!m->bank_a||!m->bank_b||m->bank_a->state!=I9100_DMA_DEVICE||m->bank_b->state!=I9100_DMA_DEVICE||
    rd(m,BANK_A)!=m->bank_a->physical||rd(m,BANK_B)!=m->bank_b->physical||rd(m,RESET)!=0x3ffU)return I9100_MFC_OWNER;
 return 0;
}
static void pending(struct i9100_mfc *m,unsigned expected)
{m->pending=1;m->expected=expected;m->started=m->io.now_ns(m->io.cookie);m->polls=0;}
static int request(struct i9100_mfc *m,unsigned command,unsigned expected,const uint32_t args[4])
{
 int e=request_ready(m);if(e)return e;
 for(unsigned n=0;n<4;n++){e=exact(m,H2ARG+4U*n,args[n]);if(e)return failed(m,e);}
 pending(m,expected);wr(m,H2R,command);return 0; /* Consumption may clear H2R immediately; only a real reply completes it. */
}
int i9100_mfc_sys_init(struct i9100_mfc *m)
{
 int e=valid(m);if(e)return e;if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 uint32_t args[4]={m->profile.system_bytes,0,0,0};e=m->state!=STATE_FW?I9100_MFC_BUSY:request(m,3,8,args);
 m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_open_h264(struct i9100_mfc *m)
{
 int e=valid(m);if(e)return e;if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 if(m->state!=STATE_INITIALIZED||m->instance_open||!m->h264_staged){e=I9100_MFC_BUSY;goto done;}
 uint32_t args[4]={0,0,m->plan.context_offset>>11,m->plan.context_bytes};e=request(m,1,1,args);
done:m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_parse_h264_header(struct i9100_mfc *m)
{
 int e=valid(m);if(e)return e;if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 if(m->state!=STATE_INITIALIZED||!m->instance_open||!m->h264_staged){e=I9100_MFC_BUSY;goto done;}
 e=request_ready(m);if(e)goto done;
 const unsigned offsets[7]={0x2044,0x2048,0x204c,0x2058,0x205c,0x2064,0x2068};
 const uint32_t values[7]={m->plan.stream_offset>>11,m->plan.stream_bytes,m->plan.descriptor_offset>>11,
  m->plan.stream_capacity,m->plan.descriptor_bytes,m->plan.shared_offset,0};
 for(unsigned n=0;n<7&&!e;n++)e=exact(m,offsets[n],values[n]);if(e){e=failed(m,e);goto done;}
 pending(m,4);m->last_channel0=(1U<<16)|m->instance;wr(m,0x2040,m->last_channel0);
done:m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_close(struct i9100_mfc *m)
{
 int e=valid(m);if(e)return e;if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 uint32_t args[4]={m->instance,0,0,0};e=m->instance_open&&!m->decoder?request(m,2,2,args):I9100_MFC_BUSY;
 m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_poll(struct i9100_mfc *m)
{
 int e=valid(m);if(e)return e;if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 if(!m->enabled){e=I9100_MFC_DISABLED;goto done;}if(m->fault){e=m->first_error?m->first_error:I9100_MFC_IO;goto done;}
 if(!m->pending)goto done;
 uint32_t raw=rd(m,R2H),interrupt=rd(m,HOST_INTERRUPT);m->last_reply=raw;m->raw_interrupt=interrupt;
 for(unsigned n=0;n<4;n++)m->result[n]=rd(m,R2ARG+4U*n);
 uint64_t now=m->io.now_ns(m->io.cookie);if(m->polls!=UINT32_MAX)m->polls++;
 if(now<m->started||now-m->started>m->profile.timeout_ms*1000000ULL||m->polls>10000U){e=I9100_MFC_TIMEOUT;goto fail;}
 if(raw==UINT32_MAX||interrupt==UINT32_MAX){e=I9100_MFC_IO;goto fail;}
 unsigned reply=raw&0x1ffffU;if(!reply||!(interrupt&1U)){e=I9100_MFC_BUSY;goto done;}
 if(rd(m,R2H)!=raw||rd(m,HOST_INTERRUPT)!=interrupt){e=I9100_MFC_REPLY;goto fail;}
 for(unsigned n=0;n<4;n++)if(rd(m,R2ARG+4U*n)!=m->result[n]){e=I9100_MFC_REPLY;goto fail;}
 if(reply==32U||m->result[1]){e=I9100_MFC_FIRMWARE;goto fail;}
 if(reply!=m->expected){e=I9100_MFC_REPLY;goto fail;}
 if(reply==9U)m->state=STATE_FW;
 else if(reply==8U){
  m->firmware_version=rd(m,FW_VERSION);m->sys_memory_bytes=m->result[0];
  if(m->firmware_version!=m->profile.expected_version||!m->sys_memory_bytes||m->sys_memory_bytes>m->profile.system_bytes)
   {e=I9100_MFC_FIRMWARE;goto fail;}m->state=STATE_INITIALIZED;
 }else if(reply==1U){if(m->result[0]>=16U){e=I9100_MFC_REPLY;goto fail;}m->instance=m->result[0];m->instance_open=1;
 }else if(reply==2U){m->instance_open=0;m->state=STATE_INITIALIZED;
 }else if(reply==4U){
  uint32_t width=rd(m,0x2008),height=rd(m,0x2004),count=rd(m,0x200c),channel=rd(m,0x2000);
  m->consumed_bytes=rd(m,0x2018);
  if(channel!=m->instance||width!=rd(m,0x2008)||height!=rd(m,0x2004)||count!=rd(m,0x200c)||
     m->consumed_bytes>m->plan.stream_bytes||i9100_mfc_h264_sizes(width,height,count,&m->sequence))
   {e=I9100_MFC_REPLY;goto fail;}m->state=STATE_HEADER;
 }else if(reply==15U||reply==5U){
  e=i9100_mfc_decoder_complete_locked(m,reply);if(e){if(m->fault)goto done;goto fail;}
 }
 e=exact(m,0x2000,0xffffU);if(!e)e=exact(m,R2H,0);if(!e)e=exact(m,HOST_INTERRUPT,0);
 if(e)goto fail;m->pending=0;goto done;
fail:e=failed(m,e);
done:m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_stop(struct i9100_mfc *m)
{
 int e=valid(m);if(e)return e;if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 e=stop_locked(m);if(!e)m->fault=0;else note(m,e);m->io.unlock(m->io.cookie);return e;
}
