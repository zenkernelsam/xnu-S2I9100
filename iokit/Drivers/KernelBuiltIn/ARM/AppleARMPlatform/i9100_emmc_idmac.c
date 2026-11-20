/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_idmac.h"
#include <string.h>
#define ID_OWN (1U<<31)
#define ID_CES (1U<<30)
#define ID_CH (1U<<4)
#define ID_FS (1U<<3)
#define ID_LD (1U<<2)
#define ID_DIC (1U<<1)
#define CTRL_DMA ((1U<<5)|(1U<<25))
#define BMOD_DMA (1U<<7)
#define ID_ERRORS 0x234U
#define ID_NORMAL (1U<<8)
#define HOST_ERRORS 0x0000bfC2U
#define FIFO_COUNT 0x3ffe0000U
static uint32_t rd(struct i9100_emmc_card *c,unsigned r){return c->io.read32(c->io.cookie,r);}
static void wr(struct i9100_emmc_card *c,unsigned r,uint32_t value){c->io.write32(c->io.cookie,r,value);}
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(8*n));}
static int valid(struct i9100_emmc_idmac *s,struct i9100_emmc_card *c)
{
 return !s||!c||s->binding.self!=(uintptr_t)&s->binding||s->binding.card!=(uintptr_t)c||
  c->dma!=&s->binding||c->self!=(uintptr_t)c||s->binding.generation!=c->generation||
  memcmp(s->binding.cid,c->cid,16)||!s->binding.enabled||s->buffer_holds!=1U||
  !s->data||!s->descriptors||s->data->holds!=1U||s->descriptors->holds!=1U?I9100_EMMC_OWNER:0;
}
int i9100_emmc_idmac_buffer_idle(struct i9100_emmc_idmac *s,struct i9100_emmc_card *c)
{
 if(!s||!c||!s->io.board_check||s->io.board_check(s->io.cookie))return I9100_EMMC_OWNER;
 uint32_t ctrl=rd(c,EM_CTRL),mode=rd(c,EM_BMOD),cmd=rd(c,EM_CMD),status=rd(c,EM_STATUS),id=rd(c,EM_IDSTS);
 if(ctrl==UINT32_MAX||mode==UINT32_MAX||cmd==UINT32_MAX||status==UINT32_MAX||id==UINT32_MAX)return I9100_EMMC_IO;
 if(ctrl&(7U|CTRL_DMA)||mode&(BMOD_DMA|1U)||cmd&(1U<<31)||status&(FIFO_COUNT|(1U<<30)|(1U<<31))||
    id&(15U<<13)||rd(c,EM_IDINTEN)||rd(c,EM_INTMASK))return I9100_EMMC_BUSY;
 return 0;
}
static int buffers_geometry(struct i9100_dma_buffer *d,struct i9100_dma_buffer *b)
{
 if(!d||!b||d==b||d->self!=(uintptr_t)d||b->self!=(uintptr_t)b||d->bytes!=I9100_IDMAC_DESC_BYTES||b->bytes!=I9100_IDMAC_DATA_BYTES||
    (d->state!=I9100_DMA_BOUND&&d->state!=I9100_DMA_CPU)||(b->state!=I9100_DMA_BOUND&&b->state!=I9100_DMA_CPU)||d->holds||b->holds||
    !d->cpu||!b->cpu||d->cpu>UINTPTR_MAX-d->bytes||b->cpu>UINTPTR_MAX-b->bytes||
    (d->cpu&63U)||(b->cpu&63U)||(d->physical&63U)||(b->physical&63U)||
    d->physical<0x40000000U||b->physical<0x40000000U||(uint64_t)d->physical+d->bytes>0x80000000ULL||(uint64_t)b->physical+b->bytes>0x80000000ULL||
    (d->physical<((uint64_t)b->physical+b->bytes)&&b->physical<((uint64_t)d->physical+d->bytes))||
    (d->cpu<b->cpu+b->bytes&&b->cpu<d->cpu+d->bytes))return I9100_EMMC_ARGUMENT;
 return 0;
}
static int wait_clear(struct i9100_emmc_card *c,unsigned reg,uint32_t mask)
{
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(unsigned i=0;i<c->io.poll_bound;i++){
  uint32_t v=rd(c,reg);if(v==UINT32_MAX)return I9100_EMMC_IO;if(!(v&mask))return 0;
  uint64_t now=c->io.now_ns(c->io.cookie);if(now<start||now-start>=50000000ULL)return I9100_EMMC_TIMEOUT;
  if(c->io.delay_us(c->io.cookie,10))return I9100_EMMC_IO;
 }
 return I9100_EMMC_TIMEOUT;
}
static void stop_once(struct i9100_emmc_idmac *s,struct i9100_emmc_card *c)
{
 if(!s->mode_started||s->mode_stopped)return;s->mode_stopped=1;
 uint32_t ctrl=rd(c,EM_CTRL),mode=rd(c,EM_BMOD);
 if(ctrl!=UINT32_MAX)wr(c,EM_CTRL,ctrl&~CTRL_DMA);
 if(mode!=UINT32_MAX)wr(c,EM_BMOD,mode&~BMOD_DMA);
 wr(c,EM_IDINTEN,0);s->io.barrier(s->io.cookie);
}
static int failed(void *cookie,struct i9100_emmc_card *c,int error)
{
 struct i9100_emmc_idmac *s=cookie;if(valid(s,c))return I9100_EMMC_OWNER;
 if(!s->error)s->error=error;s->failure_phase=s->phase;s->phase=I9100_IDMAC_FAILED;s->binding.poisoned=1;
 s->mutation_possible=s->binding.writing&&s->binding.command_submitted;
 stop_once(s,c);s->cleanup_error=i9100_emmc_idmac_buffer_idle(s,c);
 /* Never claim a completed transfer or publish partially received data.
  * Guard ownership is retained even if the controller later looks idle. */
 if(s->mode_started||s->data->handed_to_device||s->data->state==I9100_DMA_DEVICE)(void)i9100_dma_quarantine(s->data);
 if(s->mode_started||s->descriptors->handed_to_device||s->descriptors->state==I9100_DMA_DEVICE)(void)i9100_dma_quarantine(s->descriptors);
 c->last_error=s->error;c->ready=0;return s->error;
}
static int buffers_valid(struct i9100_dma_buffer *d,struct i9100_dma_buffer *b)
{
 if(!d||!b||d==b||d->self!=(uintptr_t)d||b->self!=(uintptr_t)b||d->bytes!=I9100_IDMAC_DESC_BYTES||
    b->bytes!=I9100_IDMAC_DATA_BYTES||d->state!=I9100_DMA_CPU||b->state!=I9100_DMA_CPU||
    d->physical<0x40000000U||b->physical<0x40000000U||(uint64_t)d->physical+d->bytes>0x80000000ULL||
    (uint64_t)b->physical+b->bytes>0x80000000ULL||
    (d->physical<((uint64_t)b->physical+b->bytes)&&b->physical<((uint64_t)d->physical+d->bytes))||
    (d->cpu<b->cpu+b->bytes&&b->cpu<d->cpu+d->bytes)||d->cpu>UINTPTR_MAX-d->bytes||b->cpu>UINTPTR_MAX-b->bytes||
    !d->cpu||!b->cpu||(d->cpu&63U)||(b->cpu&63U)||(d->physical&63U)||(b->physical&63U))return I9100_EMMC_ARGUMENT;
 return 0;
}
static uint32_t flags(unsigned index,unsigned total)
{return (index==0?ID_FS:0U)|(index+1U==total?ID_LD:ID_CH|ID_DIC);}
static int descriptor_returned(struct i9100_emmc_idmac *s)
{
 const volatile uint32_t *v=(const volatile uint32_t *)s->descriptors->cpu;
 s->io.barrier(s->io.cookie);
 for(unsigned i=0;i<s->descriptors_used;i++){
  if(v[i*4U]&(ID_OWN|ID_CES))return 0;
 }
 return 1;
}
static int descriptor_exact(struct i9100_emmc_idmac *s)
{
 const uint8_t *p=(const uint8_t *)s->descriptors->cpu;uint32_t remaining=s->bytes;
 for(unsigned i=0;i<s->descriptors_used;i++){
  uint32_t n=remaining>I9100_IDMAC_CHUNK?I9100_IDMAC_CHUNK:remaining;
  if(get32(p+i*16)!=flags(i,s->descriptors_used)||get32(p+i*16+4)!=n||
     get32(p+i*16+8)!=s->data->physical+i*I9100_IDMAC_CHUNK||
     get32(p+i*16+12)!=(i+1U==s->descriptors_used?0U:s->descriptors->physical+(i+1U)*16U))return I9100_EMMC_READBACK;
  remaining-=n;
 }
 return remaining?I9100_EMMC_READBACK:0;
}
static int begin(void *cookie,struct i9100_emmc_card *c,unsigned command,uint32_t arg,uint32_t sectors,int writing,const uint8_t *source)
{
 struct i9100_emmc_idmac *s=cookie;int e=valid(s,c);if(e)return e;
 if(s->binding.poisoned)return s->error?s->error:I9100_EMMC_NO_DEVICE;
 if(s->binding.active||!sectors||sectors>I9100_EMMC_MAX_SECTORS||(writing!=0&&writing!=1)||
    command!=(writing?(sectors==1?24U:25U):(sectors==1?17U:18U))||(writing&&!source))return I9100_EMMC_ARGUMENT;
 e=i9100_emmc_transport_check(c);if(e)return e;e=i9100_emmc_idmac_buffer_idle(s,c);if(e)return e;
 e=buffers_valid(s->descriptors,s->data);if(e)return e;
 if(s->attempts==UINT32_MAX)return I9100_EMMC_OWNER;s->attempts++;
 s->mode_started=s->mode_stopped=s->mutation_possible=s->binding.command_submitted=0;
 s->binding.command=command;s->binding.argument=arg;s->binding.sectors=sectors;s->binding.writing=(unsigned)writing;
 s->binding.ciu_bytes=s->binding.host_bytes=0;s->bytes=sectors*512U;s->descriptors_used=(s->bytes+I9100_IDMAC_CHUNK-1)/I9100_IDMAC_CHUNK;
 s->saved_ctrl=rd(c,EM_CTRL);s->saved_bmod=rd(c,EM_BMOD);s->saved_fifoth=rd(c,EM_FIFOTH);s->saved_dbaddr=rd(c,EM_DBADDR);
 if(s->saved_fifoth==UINT32_MAX||s->saved_dbaddr==UINT32_MAX)return failed(s,c,I9100_EMMC_IO);
 s->phase=I9100_IDMAC_PREPARED;
 wr(c,EM_CTRL,s->saved_ctrl|(1U<<1)|(1U<<2));e=wait_clear(c,EM_CTRL,6U);if(e)return failed(s,c,e);
 wr(c,EM_BMOD,s->saved_bmod|1U);e=wait_clear(c,EM_BMOD,1U);if(e)return failed(s,c,e);
 uint8_t *desc=(uint8_t *)s->descriptors->cpu;memset(desc,0,s->descriptors->bytes);
 if(writing)memcpy((void *)s->data->cpu,source,s->bytes);else memset((void *)s->data->cpu,0,s->bytes);
 for(unsigned i=0,remaining=s->bytes;i<s->descriptors_used;i++){
  uint32_t n=remaining>I9100_IDMAC_CHUNK?I9100_IDMAC_CHUNK:remaining;
  put32(desc+i*16,flags(i,s->descriptors_used)|ID_OWN);put32(desc+i*16+4,n);
  put32(desc+i*16+8,s->data->physical+i*I9100_IDMAC_CHUNK);
  put32(desc+i*16+12,i+1==s->descriptors_used?0U:s->descriptors->physical+(i+1)*16U);remaining-=n;
 }
 e=i9100_dma_prepare(s->data,writing?I9100_DMA_TO_DEVICE:I9100_DMA_FROM_DEVICE);if(e)return failed(s,c,e);
 e=i9100_dma_prepare(s->descriptors,I9100_DMA_BIDIRECTIONAL);if(e)return failed(s,c,e);
 wr(c,EM_BLKSIZ,512);wr(c,EM_BYTCNT,s->bytes);wr(c,EM_FIFOTH,0);wr(c,EM_IDSTS,0x337U);wr(c,EM_IDINTEN,0);
 wr(c,EM_DBADDR,s->descriptors->physical);s->io.barrier(s->io.cookie);
 if(rd(c,EM_BLKSIZ)!=512||rd(c,EM_BYTCNT)!=s->bytes||rd(c,EM_DBADDR)!=s->descriptors->physical||rd(c,EM_FIFOTH)!=0||rd(c,EM_IDINTEN))
  return failed(s,c,I9100_EMMC_READBACK);
 if(rd(c,EM_IDSTS)&0x337U)return failed(s,c,I9100_EMMC_READBACK);
 s->binding.active=1;s->mode_started=1;s->phase=I9100_IDMAC_ARMED;
 wr(c,EM_CTRL,s->saved_ctrl|CTRL_DMA);wr(c,EM_BMOD,s->saved_bmod|BMOD_DMA|2U);s->io.barrier(s->io.cookie);
 if(rd(c,EM_CTRL)!=(s->saved_ctrl|CTRL_DMA)||rd(c,EM_BMOD)!=(s->saved_bmod|BMOD_DMA|2U))return failed(s,c,I9100_EMMC_READBACK);
 return 0;
}
static int finish(void *cookie,struct i9100_emmc_card *c,uint8_t *output)
{
 struct i9100_emmc_idmac *s=cookie;int e=valid(s,c);if(e)return e;
 if(!s->binding.active||s->binding.poisoned||!s->binding.command_submitted||(!s->binding.writing&&!output))return I9100_EMMC_OWNER;
 s->phase=I9100_IDMAC_COMPLETING;uint64_t start=c->io.now_ns(c->io.cookie);int done=0;
 for(unsigned i=0;i<c->io.poll_bound;i++){
  s->last_rint=rd(c,EM_RINTSTS);s->last_idsts=rd(c,EM_IDSTS);s->last_status=rd(c,EM_STATUS);
  uint64_t now=c->io.now_ns(c->io.cookie);if(now<start||now-start>=500000000ULL)return failed(s,c,I9100_EMMC_TIMEOUT);
  if(s->last_rint==UINT32_MAX||s->last_idsts==UINT32_MAX||s->last_status==UINT32_MAX)return failed(s,c,I9100_EMMC_IO);
  if(s->last_rint&HOST_ERRORS)return failed(s,c,s->last_rint&(1U<<7)?I9100_EMMC_DATA_CRC:s->last_rint&(1U<<6)?I9100_EMMC_RESPONSE_CRC:s->last_rint&0x700U?I9100_EMMC_TIMEOUT:I9100_EMMC_IO);
  if(s->last_idsts&ID_ERRORS)return failed(s,c,I9100_EMMC_IO);
  uint32_t direction=s->binding.writing?1U:2U;
  if(s->last_rint&(1U<<3)&&s->last_idsts&ID_NORMAL&&s->last_idsts&direction&&!(s->last_idsts&(15U<<13))&&
     !(s->last_status&(FIFO_COUNT|(1U<<30)|(1U<<31)))&&!(rd(c,EM_CMD)&(1U<<31))&&descriptor_returned(s)){done=1;break;}
  if(c->io.delay_us(c->io.cookie,10))return failed(s,c,I9100_EMMC_IO);
 }
 if(!done)return failed(s,c,I9100_EMMC_TIMEOUT);
 s->binding.ciu_bytes=rd(c,EM_TCBCNT);s->binding.host_bytes=rd(c,EM_TBBCNT);
 if(s->binding.ciu_bytes!=s->bytes||s->binding.host_bytes!=s->bytes)return failed(s,c,I9100_EMMC_READBACK);
 stop_once(s,c);e=i9100_emmc_idmac_buffer_idle(s,c);if(e)return failed(s,c,e);
 wr(c,EM_BMOD,s->saved_bmod);wr(c,EM_CTRL,s->saved_ctrl);wr(c,EM_FIFOTH,s->saved_fifoth);wr(c,EM_DBADDR,s->saved_dbaddr);
 s->io.barrier(s->io.cookie);
 if(rd(c,EM_BMOD)!=s->saved_bmod||rd(c,EM_CTRL)!=s->saved_ctrl||rd(c,EM_FIFOTH)!=s->saved_fifoth||rd(c,EM_DBADDR)!=s->saved_dbaddr)
  return failed(s,c,I9100_EMMC_READBACK);
 e=i9100_dma_complete(s->descriptors);if(e)return failed(s,c,e);e=i9100_dma_complete(s->data);if(e)return failed(s,c,e);
 e=descriptor_exact(s);if(e)return failed(s,c,e);
 wr(c,EM_IDSTS,0x337U);wr(c,EM_RINTSTS,(1U<<3)|(1U<<4)|(1U<<5));
 s->io.barrier(s->io.cookie);
 if(rd(c,EM_IDSTS)&0x337U||rd(c,EM_RINTSTS)&((1U<<3)|(1U<<4)|(1U<<5)))return failed(s,c,I9100_EMMC_READBACK);
 s->binding.active=0;s->phase=I9100_IDMAC_BOUND;s->completed++;
 if(!s->binding.writing)memcpy(output,(const void *)s->data->cpu,s->bytes);return 0;
}
int i9100_emmc_idmac_bind(int enabled,struct i9100_emmc_idmac *s,struct i9100_emmc_card *c,
 const struct i9100_emmc_idmac_io *io,struct i9100_dma_buffer *desc,struct i9100_dma_buffer *data)
{
 if(!enabled)return I9100_EMMC_UNSUPPORTED;
 if(enabled!=1||!s||!c||!io||!io->board_check||!io->barrier||io->descriptors_normal_nc!=1U||s->binding.self||c->dma||
    c->self!=(uintptr_t)c||!c->ready||((c->hcon>>7)&7U)!=1U||((c->hcon>>16)&3U)!=0U||
    (c->version&65535U)<0x2000U||(c->version&65535U)>=0x240aU)return I9100_EMMC_ARGUMENT;
 if(i9100_emmc_transport_check(c)||io->board_check(io->cookie))return I9100_EMMC_OWNER;
 int geometry=buffers_geometry(desc,data);if(geometry)return geometry;
 int e=desc->state==I9100_DMA_BOUND?i9100_dma_enable(desc,1):0;if(e)return e;
 e=data->state==I9100_DMA_BOUND?i9100_dma_enable(data,1):0;if(e)return e;
 e=buffers_valid(desc,data);if(e)return e;
 e=i9100_dma_hold_pair(desc,data);if(e)return e;
 memset(s,0,sizeof(*s));s->binding.self=(uintptr_t)&s->binding;s->binding.card=(uintptr_t)c;s->binding.generation=c->generation;
 memcpy(s->binding.cid,c->cid,16);s->binding.enabled=1;s->binding.cookie=s;s->binding.begin=begin;s->binding.finish=finish;s->binding.fail=failed;
 s->io=*io;s->descriptors=desc;s->data=data;s->phase=I9100_IDMAC_BOUND;
 s->buffer_holds=1;c->dma=&s->binding;
 e=i9100_emmc_idmac_buffer_idle(s,c);return e?failed(s,c,e):0;
}
int i9100_emmc_idmac_unbind(struct i9100_emmc_idmac *s,struct i9100_emmc_card *c)
{
 int e=valid(s,c);if(e)return e;if(s->binding.poisoned)return s->error?s->error:I9100_EMMC_NO_DEVICE;
 if(s->binding.active||c->write_grant)return I9100_EMMC_BUSY;e=i9100_emmc_idmac_buffer_idle(s,c);if(e)return e;
 uint32_t status=rd(c,EM_STATUS);if(status==UINT32_MAX)return I9100_EMMC_IO;
 if(status&((15U<<4)|(1U<<9)|(1U<<10)))return I9100_EMMC_BUSY;
 if(s->data->state!=I9100_DMA_CPU||s->descriptors->state!=I9100_DMA_CPU)return I9100_EMMC_BUSY;
 e=i9100_dma_unhold_pair(s->descriptors,s->data);if(e)return e;s->buffer_holds=0;
 c->dma=0;s->binding.enabled=0;s->phase=I9100_IDMAC_CLOSED;return 0;
}
