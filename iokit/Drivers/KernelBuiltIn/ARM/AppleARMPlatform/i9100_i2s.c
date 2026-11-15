/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_i2s.h"
#include <string.h>
enum {CONTROL=0,MODE=4,FIFO=8,PRESCALER=12,ACTIVE=1,RXDMA=2,TXDMA=4,
 PAUSE_RX_CHANNEL=8,PAUSE_TX_CHANNEL=16,PAUSE_RX_DMA=32,PAUSE_TX_DMA=64,
 SECONDARY_DMA=1U<<18,MODE_RX=1U<<8};
#define RESET_CLEAR (1U<<31)
#define MODE_FIELDS ((3U<<30)|(1U<<28)|(3U<<26)|(3U<<24)|(63U<<16)|(3U<<13)|0x1fffU)
#define INTERRUPT_ENABLES ((1U<<25)|(1U<<23)|(1U<<16))
static uint32_t rd(struct i9100_i2s *s,unsigned off){return s->io.read32(s->io.cookie,off);}
static void wr(struct i9100_i2s *s,unsigned off,uint32_t value){s->io.write32(s->io.cookie,off,value);}
int i9100_i2s_init(struct i9100_i2s *s,const struct i9100_i2s_io *io)
{
 if(!s||s->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||!io->dma_idle||!io->dma_stop)
  return I9100_I2S_ARGUMENT;
 memset(s,0,sizeof(*s));s->io=*io;s->initialized=1;return 0;
}
int i9100_i2s_enable(struct i9100_i2s *s,int enabled)
{
 if(!s||!s->initialized||(enabled!=0&&enabled!=1))return I9100_I2S_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_I2S_BUSY;
 int e=s->active||s->prepared||s->fault?I9100_I2S_BUSY:0;
 if(!e)s->enabled=enabled;s->io.unlock(s->io.cookie);return e;
}
int i9100_i2s_configure(struct i9100_i2s *s,uint32_t rate,uint32_t bclk,int verified)
{
 if(!s||!s->initialized||(rate!=8000U&&rate!=16000U&&rate!=44100U&&rate!=48000U))return I9100_I2S_ARGUMENT;
 if(!verified||bclk!=rate*32U)return I9100_I2S_CLOCK;
 if(s->io.lock(s->io.cookie))return I9100_I2S_BUSY;
 int e=0;
 if(!s->enabled){e=I9100_I2S_DISABLED;goto done;}
 if(s->prepared||s->active||s->fault||s->io.dma_idle(s->io.cookie)||
    (rd(s,CONTROL)&(ACTIVE|RXDMA|TXDMA|SECONDARY_DMA|INTERRUPT_ENABLES))){e=I9100_I2S_BUSY;goto done;}
 s->saved[0]=rd(s,CONTROL)&(RESET_CLEAR|0x78U);s->saved[1]=rd(s,MODE);s->saved[2]=rd(s,PRESCALER);
 /* Stop both main directions; keep unrelated read-only status out of writes. */
 wr(s,CONTROL,RESET_CLEAR|0x78U);
 uint32_t mode=(s->saved[1]&~MODE_FIELDS)|(3U<<30)|(1U<<12)|(1U<<11)|(1U<<10);
 wr(s,MODE,mode);wr(s,PRESCALER,0);wr(s,FIFO,(1U<<15)|(1U<<7));wr(s,FIFO,0);
 s->prepared=1;s->sample_rate=rate;
 if(rd(s,MODE)!=mode||(rd(s,CONTROL)&(ACTIVE|RXDMA|TXDMA|SECONDARY_DMA))){e=I9100_I2S_IO;s->fault=1;}
done:
 s->io.unlock(s->io.cookie);return e;
}
int i9100_i2s_start(struct i9100_i2s *s,int capture,int muted)
{
 if(!s||!s->initialized||(capture!=0&&capture!=1)||muted!=1)return I9100_I2S_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_I2S_BUSY;
 int e=0;
 if(!s->enabled||!s->prepared){e=I9100_I2S_DISABLED;goto done;}
 if(s->active||s->fault){e=I9100_I2S_BUSY;goto done;}
 wr(s,MODE,(rd(s,MODE)&~(3U<<8))|(capture?MODE_RX:0U));
 uint32_t control=RESET_CLEAR|ACTIVE|(capture?(RXDMA|PAUSE_TX_CHANNEL|PAUSE_TX_DMA):(TXDMA|PAUSE_RX_CHANNEL|PAUSE_RX_DMA));
 wr(s,CONTROL,control);s->active=1;
 if((rd(s,CONTROL)&7U)!=(control&7U)){e=I9100_I2S_IO;s->fault=1;}
done:
 s->io.unlock(s->io.cookie);return e;
}
int i9100_i2s_stop(struct i9100_i2s *s)
{
 if(!s||!s->initialized)return I9100_I2S_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_I2S_BUSY;
 int e=0;
 if(!s->prepared){e=I9100_I2S_DISABLED;goto done;}
 wr(s,CONTROL,RESET_CLEAR|0x78U);
 if(s->io.dma_stop(s->io.cookie)||s->io.dma_idle(s->io.cookie)||(rd(s,CONTROL)&7U))
  {e=I9100_I2S_IO;s->fault=1;goto done;}
 wr(s,FIFO,(1U<<15)|(1U<<7));wr(s,FIFO,0);
 wr(s,MODE,s->saved[1]);wr(s,PRESCALER,s->saved[2]);wr(s,CONTROL,s->saved[0]);
 if(rd(s,MODE)!=s->saved[1]||rd(s,PRESCALER)!=s->saved[2]){e=I9100_I2S_IO;s->fault=1;}
 else s->prepared=s->active=s->fault=0;
done:
 s->io.unlock(s->io.cookie);return e;
}
void i9100_pcm_attenuate_30db(int16_t *destination,const int16_t *source,unsigned samples)
{
 if(!destination||!source||samples>131072U)return;
 /* 1036/32768 <= 10^(-30/20); all int16 inputs fit a signed32 product. */
 for(unsigned n=0;n<samples;n++)destination[n]=(int16_t)((int32_t)source[n]*1036/32768);
}
