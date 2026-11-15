/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audss.h"
#include <string.h>
enum {SOURCE=0,DIVIDER=4,GATE=8,SOURCE_MASK=13,DIV_MASK=0xfff,I2S_GATES=0x4c};
static uint32_t rd(struct i9100_audss *a,unsigned off){return a->io.read32(a->io.cookie,off);}
static void wr(struct i9100_audss *a,unsigned off,uint32_t v){a->io.write32(a->io.cookie,off,v);}
static int decode(struct i9100_audss *a,uint32_t source,uint32_t divider)
{
 uint32_t main=source&1U?a->rates.epll:a->rates.xtal;
 uint32_t i2s;
 switch((source>>2)&3U){case 0:i2s=main;break;case 1:i2s=a->rates.external_i2s;break;
 case 2:i2s=a->rates.sclk_audio0;break;default:return I9100_AUDSS_CLOCK;}
 if(!main||!i2s)return I9100_AUDSS_CLOCK;
 a->rates.bus=main/((divider&15U)+1U)/(((divider>>4)&15U)+1U);
 a->rates.i2s=i2s/(((divider>>8)&15U)+1U);
 if(!a->rates.bus||a->rates.bus>100000000U||!a->rates.i2s||a->rates.i2s>100000000U)
  return I9100_AUDSS_CLOCK;
 return 0;
}
int i9100_audss_init(struct i9100_audss *a,const struct i9100_audss_io *io,const struct i9100_audss_rates *rates)
{
 if(!a||a->initialized||!io||!rates||!rates->xtal||rates->xtal>50000000U||
    rates->epll>400000000U||rates->external_i2s>100000000U||rates->sclk_audio0>400000000U||
    !io->read32||!io->write32||!io->lock||!io->unlock||!io->domain_get||!io->domain_put||!io->consumers_idle)
  return I9100_AUDSS_ARGUMENT;
 memset(a,0,sizeof(*a));a->io=*io;a->rates=*rates;a->initialized=1;return 0;
}
int i9100_audss_enable(struct i9100_audss *a,int enabled)
{
 if(!a||!a->initialized||(enabled!=0&&enabled!=1))return I9100_AUDSS_ARGUMENT;
 if(a->io.lock(a->io.cookie))return I9100_AUDSS_BUSY;
 int e=a->active||a->domain_owned?I9100_AUDSS_BUSY:0;
 if(!e)a->enabled=enabled;a->io.unlock(a->io.cookie);return e;
}
int i9100_audss_acquire(struct i9100_audss *a,const struct i9100_audss_config *c)
{
 if(!a||!a->initialized)return I9100_AUDSS_ARGUMENT;
 if(c&&(c->main_source>1U||c->i2s_source>2U||!c->rp_div||c->rp_div>16U||
    !c->bus_div||c->bus_div>16U||!c->i2s_div||c->i2s_div>16U))return I9100_AUDSS_ARGUMENT;
 if(a->io.lock(a->io.cookie))return I9100_AUDSS_BUSY;
 int e=0;
 if(!a->enabled){e=I9100_AUDSS_DISABLED;goto done;}
 if(a->active||a->domain_owned||a->fault){e=I9100_AUDSS_BUSY;goto done;}
 if(a->io.domain_get(a->io.cookie)){e=I9100_AUDSS_IO;goto done;}
 a->domain_owned=1;
 for(unsigned n=0;n<3;n++)a->saved[n]=rd(a,n*4U);
 if(c&&(a->saved[2]&0x1ffU)){e=I9100_AUDSS_BUSY;goto unwind;}
 uint32_t source=a->saved[0],divider=a->saved[1];
 if(c){source=(source&~SOURCE_MASK)|c->main_source|((uint32_t)c->i2s_source<<2);
  divider=(divider&~DIV_MASK)|(c->rp_div-1U)|((uint32_t)(c->bus_div-1U)<<4)|((uint32_t)(c->i2s_div-1U)<<8);}
 e=decode(a,source,divider);if(e)goto unwind;
 if(c){a->reconfigured=1;wr(a,SOURCE,source);wr(a,DIVIDER,divider);
  if((rd(a,SOURCE)&SOURCE_MASK)!=(source&SOURCE_MASK)||(rd(a,DIVIDER)&DIV_MASK)!=(divider&DIV_MASK))
   {e=I9100_AUDSS_IO;goto unwind;}}
 wr(a,GATE,a->saved[2]|I2S_GATES);
 if((rd(a,GATE)&I2S_GATES)!=I2S_GATES){e=I9100_AUDSS_IO;goto unwind;}
 a->active=1;goto done;
unwind:
 wr(a,GATE,(rd(a,GATE)&~I2S_GATES)|(a->saved[2]&I2S_GATES));
 if(a->reconfigured){wr(a,SOURCE,a->saved[0]);wr(a,DIVIDER,a->saved[1]);a->reconfigured=0;}
 if((rd(a,GATE)&I2S_GATES)!=(a->saved[2]&I2S_GATES)||a->io.domain_put(a->io.cookie))a->fault=1;
 else a->domain_owned=0;
done:
 a->io.unlock(a->io.cookie);return e;
}
int i9100_audss_release(struct i9100_audss *a)
{
 if(!a||!a->initialized||!a->domain_owned)return I9100_AUDSS_ARGUMENT;
 if(a->io.lock(a->io.cookie))return I9100_AUDSS_BUSY;
 int e=0;
 if(a->io.consumers_idle(a->io.cookie)){e=I9100_AUDSS_BUSY;goto done;}
 wr(a,GATE,(rd(a,GATE)&~I2S_GATES)|(a->saved[2]&I2S_GATES));
 if((rd(a,GATE)&I2S_GATES)!=(a->saved[2]&I2S_GATES)){e=I9100_AUDSS_IO;goto done;}
 if(a->reconfigured){
  /* Other users must not start AUDSS while this exclusive lease is live. */
  if(rd(a,GATE)&0x1ffU){e=I9100_AUDSS_BUSY;goto done;}
  wr(a,SOURCE,a->saved[0]);wr(a,DIVIDER,a->saved[1]);
  if(rd(a,SOURCE)!=a->saved[0]||rd(a,DIVIDER)!=a->saved[1]){e=I9100_AUDSS_IO;goto done;}
  a->reconfigured=0;
 }
 if(a->io.domain_put(a->io.cookie)){e=I9100_AUDSS_IO;goto done;}
 a->domain_owned=a->active=a->fault=0;
done:
 if(e==I9100_AUDSS_IO)a->fault=1;
 a->io.unlock(a->io.cookie);return e;
}
