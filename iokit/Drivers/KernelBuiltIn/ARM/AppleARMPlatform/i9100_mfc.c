/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc.h"
#include <string.h>
enum {RESET=0,HOST_INTERRUPT=8,H2R=0x30,H2ARG=0x34,R2H=0x44,R2ARG=0x48,FW_VERSION=0x58,
 BANK_A=0x508,BANK_B=0x50c,MC_STATUS=0x510,STATE_FW=1,STATE_INITIALIZED=2};
static uint32_t rd(struct i9100_mfc *m,unsigned off){return m->io.read32(m->io.cookie,off);}
static void wr(struct i9100_mfc *m,unsigned off,uint32_t value){m->io.write32(m->io.cookie,off,value);m->io.barrier(m->io.cookie);}
static int mc_idle(struct i9100_mfc *m)
{
 uint64_t start=m->io.now_ns(m->io.cookie);
 for(unsigned n=0;n<10000U;n++){
  if(!(rd(m,MC_STATUS)&3U))return 0;
  uint64_t now=m->io.now_ns(m->io.cookie);if(now<start||now-start>100000000ULL)break;
  if(m->io.delay_us(m->io.cookie,10))return I9100_MFC_IO;
 }
 return I9100_MFC_TIMEOUT;
}
int i9100_mfc_idle(void *cookie)
{
 struct i9100_mfc *m=cookie;if(!m||!m->initialized||m->risc_running)return -1;
 return (rd(m,RESET)&1U)||(rd(m,MC_STATUS)&3U)?-1:0;
}
static int stop_locked(struct i9100_mfc *m)
{
 if(!m->risc_running&&!m->bank_a)return 0;
 wr(m,RESET,0x3f6);wr(m,RESET,0x3e2);
 int e=m->io.delay_us(m->io.cookie,10000)?I9100_MFC_IO:mc_idle(m);
 if(!e){wr(m,RESET,0);if(rd(m,RESET)&1U)e=I9100_MFC_IO;}
 if(!e)m->risc_running=0;
 struct i9100_dma_buffer *buffers[2]={m->bank_a,m->bank_b};
 for(unsigned n=0;n<2;n++)if(buffers[n]&&(buffers[n]->state==I9100_DMA_DEVICE||buffers[n]->state==I9100_DMA_QUARANTINED))
  if(i9100_dma_abort(buffers[n])&&!e)e=I9100_MFC_IO;
 if(e){m->fault=1;return e;}
 m->bank_a=m->bank_b=0;m->pending=m->state=m->instance_open=m->fault=0;return 0;
}
int i9100_mfc_init(struct i9100_mfc *m,const struct i9100_mfc_io *io)
{
 if(!m||m->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||!io->now_ns||!io->delay_us||!io->barrier)
  return I9100_MFC_ARGUMENT;
 memset(m,0,sizeof(*m));m->io=*io;m->initialized=1;return 0;
}
int i9100_mfc_enable(struct i9100_mfc *m,int enabled,int verified)
{
 if(!m||!m->initialized||(enabled!=0&&enabled!=1))return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 int e=0;
 if(enabled){if(!verified)e=I9100_MFC_ARGUMENT;else if(m->enabled||m->fault||rd(m,H2R)||(rd(m,MC_STATUS)&3U)||(rd(m,RESET)&1U))e=I9100_MFC_BUSY;else m->enabled=1;}
 else if(!m->enabled)e=I9100_MFC_DISABLED;
 else{e=stop_locked(m);if(!e)m->enabled=0;}
 m->io.unlock(m->io.cookie);return e;
}
static int arena(struct i9100_dma_buffer *b)
{return b&&b->self==(uintptr_t)b&&b->state==I9100_DMA_CPU&&!(b->physical&8191U)&&b->bytes>=1024U*1024U;}
int i9100_mfc_boot(struct i9100_mfc *m,struct i9100_dma_buffer *a,struct i9100_dma_buffer *b,uint32_t firmware)
{
 if(!m||!m->initialized||!arena(a)||!arena(b)||!firmware||firmware>512U*1024U||firmware>a->bytes||
    (a->physical<(uint64_t)b->physical+b->bytes&&b->physical<(uint64_t)a->physical+a->bytes))return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 int e=0;if(!m->enabled){e=I9100_MFC_DISABLED;goto done;}if(m->fault||m->risc_running||m->bank_a){e=I9100_MFC_BUSY;goto done;}
 wr(m,RESET,0x3f6);wr(m,RESET,0x3e2);e=m->io.delay_us(m->io.cookie,10000)?I9100_MFC_IO:mc_idle(m);if(e)goto done;
 wr(m,RESET,0);wr(m,RESET,0x3fe);wr(m,BANK_A,a->physical);wr(m,BANK_B,b->physical);
 wr(m,0x2040,0xffffffffU);wr(m,0x2080,0xffffffffU);wr(m,R2H,0);wr(m,H2R,0);
 m->bank_a=a;m->bank_b=b;m->firmware_bytes=firmware;
 e=i9100_dma_prepare(a,I9100_DMA_BIDIRECTIONAL);if(!e)e=i9100_dma_prepare(b,I9100_DMA_BIDIRECTIONAL);
 if(e){e=I9100_MFC_IO;(void)stop_locked(m);goto done;}
 m->risc_running=1;m->pending=1;m->expected=9;m->started=m->io.now_ns(m->io.cookie);
 wr(m,RESET,0x3ff);
done:
 if(e&&e!=I9100_MFC_DISABLED&&e!=I9100_MFC_BUSY)m->fault=1;
 m->io.unlock(m->io.cookie);return e;
}
static int request(struct i9100_mfc *m,unsigned command,unsigned expected,const uint32_t args[4])
{
 if(!m->enabled||!m->risc_running)return I9100_MFC_DISABLED;
 if(m->fault||m->pending||rd(m,H2R)||rd(m,R2H))return I9100_MFC_BUSY;
 for(unsigned n=0;n<4;n++)wr(m,H2ARG+4U*n,args[n]);
 m->pending=1;m->expected=expected;m->started=m->io.now_ns(m->io.cookie);wr(m,H2R,command);return 0;
}
int i9100_mfc_sys_init(struct i9100_mfc *m)
{
 if(!m||!m->initialized)return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 uint32_t args[4]={m->firmware_bytes,0,0,0};int e=m->state!=STATE_FW?I9100_MFC_BUSY:request(m,3,8,args);
 m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_open_h264(struct i9100_mfc *m,uint32_t offset,uint32_t bytes)
{
 if(!m||!m->initialized||!bytes||(offset&8191U)||(bytes&8191U))return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 int e=0;
 if(m->state!=STATE_INITIALIZED||m->instance_open||!m->bank_a){e=I9100_MFC_BUSY;goto done;}
 if(offset<((m->firmware_bytes+8191U)&~8191U)||offset>m->bank_a->bytes||bytes>m->bank_a->bytes-offset)
  {e=I9100_MFC_ARGUMENT;goto done;}
 uint32_t args[4]={0,0,offset>>11,bytes};e=request(m,1,1,args);
done:
 m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_close(struct i9100_mfc *m)
{
 if(!m||!m->initialized)return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 uint32_t args[4]={m->instance,0,0,0};int e=m->instance_open?request(m,2,2,args):I9100_MFC_BUSY;
 m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_poll(struct i9100_mfc *m)
{
 if(!m||!m->initialized)return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 int e=0;
 if(!m->enabled){e=I9100_MFC_DISABLED;goto done;}if(m->fault){e=I9100_MFC_IO;goto done;}
 if(!m->pending)goto done;
 unsigned reply=rd(m,R2H)&0x1ffffU;
 if(!reply){uint64_t now=m->io.now_ns(m->io.cookie);
  if(now<m->started||now-m->started>500000000ULL){e=I9100_MFC_TIMEOUT;m->fault=1;(void)stop_locked(m);}else e=I9100_MFC_BUSY;
  goto done;}
 for(unsigned n=0;n<4;n++)m->result[n]=rd(m,R2ARG+4U*n);
 if((rd(m,R2H)&0x1ffffU)!=reply){e=I9100_MFC_REPLY;goto fail;}
 m->last_reply=reply;wr(m,HOST_INTERRUPT,0);wr(m,R2H,0);
 if(reply==32U||m->result[1]){e=I9100_MFC_FIRMWARE;goto fail;}
 if(reply!=m->expected){e=I9100_MFC_REPLY;goto fail;}
 if(reply==9U){m->firmware_version=rd(m,FW_VERSION);if(!m->firmware_version){e=I9100_MFC_FIRMWARE;goto fail;}m->state=STATE_FW;}
 else if(reply==8U)m->state=STATE_INITIALIZED;
 else if(reply==1U){if(m->result[0]>=16U){e=I9100_MFC_REPLY;goto fail;}m->instance=m->result[0];m->instance_open=1;}
 else if(reply==2U)m->instance_open=0;
 m->pending=0;goto done;
fail:
 m->fault=1;(void)stop_locked(m);
done:
 m->io.unlock(m->io.cookie);return e;
}
int i9100_mfc_stop(struct i9100_mfc *m)
{
 if(!m||!m->initialized)return I9100_MFC_ARGUMENT;
 if(m->io.lock(m->io.cookie))return I9100_MFC_BUSY;
 int e=stop_locked(m);m->io.unlock(m->io.cookie);return e;
}
