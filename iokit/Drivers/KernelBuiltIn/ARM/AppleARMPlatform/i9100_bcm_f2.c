/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_f2.h"
#include <string.h>
enum {STAGE_IDLE=0,STAGE_HEADER=1,STAGE_BODY=2,STAGE_TX=3,STAGE_DONE=4};
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static int take(void *cookie)
{struct i9100_bcm_f2 *t=cookie;return t->io.lock(t->io.cookie);}
static void drop(void *cookie)
{struct i9100_bcm_f2 *t=cookie;t->io.unlock(t->io.cookie);}
static uint64_t now(void *cookie)
{struct i9100_bcm_f2 *t=cookie;return t->io.now_ns(t->io.cookie);}
static int delay(void *cookie,uint32_t us)
{struct i9100_bcm_f2 *t=cookie;return t->io.delay_us(t->io.cookie,us);}
static int alive(struct i9100_bcm_f2 *t)
{return t->initialized&&t->card->active&&!t->card->fault&&!t->fault;}
static int fail(struct i9100_bcm_f2 *t,int error)
{t->fault=1;t->last_error=error;return error;}
static int forward52(void *cookie,int write,unsigned fn,uint32_t addr,uint8_t *value)
{
 struct i9100_bcm_f2 *t=cookie;
 if(!alive(t))return I9100_F2_IO;
 int e=i9100_sdio_cmd52(t->card,write,fn,addr,value);if(e)fail(t,I9100_F2_IO);return e;
}
static int forward53(void *cookie,int write,unsigned fn,uint32_t addr,int increment,uint8_t *bytes,uint32_t length)
{
 struct i9100_bcm_f2 *t=cookie;
 if(!alive(t)||(fn==2U&&!t->enabled))return I9100_F2_IO;
 int e=i9100_sdio_cmd53(t->card,write,fn,addr,increment,bytes,length);if(e)fail(t,I9100_F2_IO);return e;
}
static int fifo(struct i9100_bcm_f2 *t,int write,uint8_t *bytes,uint32_t length)
{
 t->last_transfer=length;
 /* For512 bytes SDIO encodes byte-count0. Larger transfers have already
  * been rounded to complete512B blocks. F2SYNC is FIXED+4BYTE, address8000. */
 int e=i9100_sdio_cmd53(t->card,write,2,0x8000U,0,bytes,length);
 return e?fail(t,I9100_F2_IO):0;
}
static uint32_t rounded(uint32_t bytes)
{return bytes>512U?(bytes+511U)&~511U:(bytes+3U)&~3U;}
static int send(void *cookie,const uint8_t *frame,uint32_t bytes)
{
 struct i9100_bcm_f2 *t=cookie;
 /* The BCM core holds this same semantic lock for the whole frame. */
 if(!t->enabled)return I9100_F2_DISABLED;if(!alive(t))return I9100_F2_IO;
 if(!frame||bytes<12U||bytes>I9100_F2_FRAME_MAX)return I9100_F2_ARGUMENT;
 uint32_t length=le16(frame),inverse=le16(frame+2);
 if((uint16_t)(length^inverse)!=0xffffU||length<12U||length>bytes||frame[7]<12U||frame[7]>length)
  return I9100_F2_FRAME;
 uint32_t wire=rounded(bytes);if(wire>sizeof(t->scratch))return I9100_F2_ARGUMENT;
 memset(t->scratch,0,wire);memcpy(t->scratch,frame,bytes);
 t->stage=STAGE_TX;t->logical_bytes=length;t->wire_bytes=wire;
 memset(t->last_header,0,sizeof(t->last_header));memcpy(t->last_header,frame,bytes<32U?bytes:32U);
 int e=fifo(t,1,t->scratch,wire);
 if(!e){if(t->tx_frames!=UINT32_MAX)t->tx_frames++;t->stage=STAGE_DONE;t->last_error=0;}
 return e;
}
int i9100_bcm_f2_init(struct i9100_bcm_f2 *t,struct i9100_sdio *card,const struct i9100_f2_io *io)
{
 if(!t||t->initialized||!card||!card->initialized||!io||!io->lock||!io->unlock||!io->now_ns||!io->delay_us)return I9100_F2_ARGUMENT;
 memset(t,0,sizeof(*t));t->io=*io;t->card=card;t->initialized=1;return 0;
}
int i9100_bcm_f2_enable(struct i9100_bcm_f2 *t,int enabled,uint32_t generation)
{
 if(!t||!t->initialized||(enabled!=0&&enabled!=1))return I9100_F2_ARGUMENT;
 if(take(t))return I9100_F2_BUSY;
 int e=0;
 if(!enabled){t->enabled=0;goto done;} /* no hardware/FIFO reset pretended */
 if(t->enabled){e=I9100_F2_BUSY;goto done;}
 if(!generation||generation<=t->generation){e=I9100_F2_GENERATION;goto done;}
 if(!t->card->active||t->card->fault||t->card->block_size[1]!=64U||t->card->block_size[2]!=512U)
  {e=I9100_F2_DISABLED;goto done;}
 uint8_t ready=0,enable=0,low=0,high=0;
 if(i9100_sdio_cmd52(t->card,0,0,2,&enable)||i9100_sdio_cmd52(t->card,0,0,3,&ready)||
    i9100_sdio_cmd52(t->card,0,0,0x210,&low)||i9100_sdio_cmd52(t->card,0,0,0x211,&high))
  {e=fail(t,I9100_F2_IO);goto done;}
 if((ready&6U)!=6U||(enable&6U)!=6U||((uint16_t)low|((uint16_t)high<<8))!=512U)
  {e=I9100_F2_DISABLED;goto done;}
 t->generation=generation;t->enabled=1;t->fault=0;t->stage=STAGE_IDLE;t->last_error=0;
done:
 drop(t);return e;
}
int i9100_bcm_f2_get_io(struct i9100_bcm_f2 *t,struct i9100_bcm_io *io)
{
 if(!t||!t->initialized||!io)return I9100_F2_ARGUMENT;
 memset(io,0,sizeof(*io));io->cookie=t;io->claim=take;io->release=drop;
 io->cmd52=forward52;io->cmd53=forward53;io->send_frame=send;io->now_ns=now;io->delay_us=delay;return 0;
}
int i9100_bcm_f2_receive(struct i9100_bcm_f2 *t,uint8_t *frame,unsigned capacity,unsigned *bytes)
{
 if(bytes)*bytes=0;
 if(!t||!t->initialized||!frame||capacity<I9100_F2_FRAME_MAX||!bytes)return I9100_F2_ARGUMENT;
 if(take(t))return I9100_F2_BUSY;
 int e=0;
 if(!t->enabled){e=I9100_F2_DISABLED;goto done;}if(!alive(t)){e=I9100_F2_IO;goto done;}
 t->stage=STAGE_HEADER;t->logical_bytes=t->wire_bytes=0;
 e=fifo(t,0,t->scratch,32);if(e)goto done;
 memcpy(t->last_header,t->scratch,32);
 uint32_t length=le16(t->scratch),inverse=le16(t->scratch+2);
 if(!(length|inverse)){if(t->empty_reads!=UINT32_MAX)t->empty_reads++;t->stage=STAGE_IDLE;goto done;}
 if((uint16_t)(length^inverse)!=0xffffU||length<12U||length>I9100_F2_FRAME_MAX||
    t->scratch[7]<12U||t->scratch[7]>length){e=fail(t,I9100_F2_FRAME);goto done;}
 if((t->scratch[5]&15U)>2U||(t->scratch[5]&0xf0U)){e=fail(t,I9100_F2_UNSUPPORTED);goto done;}
 t->logical_bytes=length;
 uint32_t remainder=length>32U?rounded(length-32U):0U;
 if(remainder>sizeof(t->scratch)-32U){e=fail(t,I9100_F2_FRAME);goto done;}
 t->wire_bytes=32U+remainder;
 if(remainder){t->stage=STAGE_BODY;e=fifo(t,0,t->scratch+32U,remainder);if(e)goto done;}
 memcpy(frame,t->scratch,length);*bytes=length;t->stage=STAGE_DONE;t->last_error=0;
 if(t->rx_frames!=UINT32_MAX)t->rx_frames++;
done:
 drop(t);return e;
}
