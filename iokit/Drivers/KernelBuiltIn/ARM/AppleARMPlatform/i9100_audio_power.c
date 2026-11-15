/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_power.h"
#include <string.h>
enum {DEBUG=0xa00,CONFIG=0x3cc0,STATUS=0x3cc4,MCLK_MASK=0xf01,MCLK_VALUE=0x900,POWER_MASK=7};
static int io_valid(const struct i9100_audio_pmu_io *io)
{return io&&io->read32&&io->write32&&io->lock&&io->unlock&&io->now_ns&&io->delay_us&&io->lease_valid&&io->consumers_idle;}
static int valid(uintptr_t self,void *object,unsigned initialized)
{return !object||!initialized?I9100_AP_ARGUMENT:self!=(uintptr_t)object?I9100_AP_OWNER:0;}
static void field(const struct i9100_audio_pmu_io *io,unsigned off,unsigned mask,uint32_t value)
{uint32_t old=io->read32(io->cookie,off);io->write32(io->cookie,off,(old&~mask)|(value&mask));}
static int settle(const struct i9100_audio_pmu_io *io,unsigned duration_us)
{
 uint64_t start=io->now_ns(io->cookie);
 for(unsigned n=0;n<200U;n++){
  uint64_t now=io->now_ns(io->cookie);if(now<start)return I9100_AP_TIMEOUT;
  if(now-start>=(uint64_t)duration_us*1000ULL)return 0;
  if(io->delay_us(io->cookie,100))return I9100_AP_IO;
 }
 return I9100_AP_TIMEOUT;
}
static int status(const struct i9100_audio_pmu_io *io,unsigned expected)
{
 uint64_t start=io->now_ns(io->cookie);
 for(unsigned n=0;n<1001U;n++){
  if((io->read32(io->cookie,STATUS)&POWER_MASK)==expected)return 0;
  uint64_t now=io->now_ns(io->cookie);
  if(now<start||now-start>=1000000ULL)return I9100_AP_TIMEOUT;
  if(io->delay_us(io->cookie,1))return I9100_AP_IO;
 }
 return I9100_AP_TIMEOUT;
}
int i9100_audio_mclk_init(struct i9100_audio_mclk *m,const struct i9100_audio_pmu_io *io,uint32_t hz)
{
 if(!m||m->initialized||!io_valid(io)||hz!=24000000U)return I9100_AP_ARGUMENT;
 memset(m,0,sizeof(*m));m->io=*io;m->self=(uintptr_t)m;m->initialized=1;m->verified_xtal_hz=hz;return 0;
}
int i9100_audio_mclk_enable(struct i9100_audio_mclk *m,int enabled)
{
 if(!m)return I9100_AP_ARGUMENT;int e=valid(m->self,m,m->initialized);if(e)return e;
 if(enabled!=0&&enabled!=1)return I9100_AP_ARGUMENT;if(m->io.lock(m->io.cookie))return I9100_AP_BUSY;
 e=m->owned||m->holds?I9100_AP_BUSY:0;if(!e)m->enabled=enabled;m->io.unlock(m->io.cookie);return e;
}
int i9100_audio_mclk_acquire(struct i9100_audio_mclk *m)
{
 if(!m)return I9100_AP_ARGUMENT;int e=valid(m->self,m,m->initialized);if(e)return e;
 if(m->io.lock(m->io.cookie))return I9100_AP_BUSY;
 if(!m->enabled){e=I9100_AP_DISABLED;goto done;}
 if(m->owned||m->fault||m->holds){e=I9100_AP_BUSY;goto done;}
 if(m->io.lease_valid(m->io.cookie)){e=I9100_AP_OWNER;goto done;}
 if(m->io.consumers_idle(m->io.cookie)){e=I9100_AP_BUSY;goto done;}
 uint32_t original=m->io.read32(m->io.cookie,DEBUG)&MCLK_MASK;
 if(!(original&1U)&&original!=MCLK_VALUE){e=I9100_AP_BUSY;goto done;}
 m->saved=original;m->owned=1;m->ready=0;m->error=0;
 if(original!=MCLK_VALUE)field(&m->io,DEBUG,MCLK_MASK,MCLK_VALUE);
 if((m->io.read32(m->io.cookie,DEBUG)&MCLK_MASK)!=MCLK_VALUE){e=I9100_AP_IO;goto failed;}
 e=settle(&m->io,10000);if(e)goto failed;
 if(m->io.lease_valid(m->io.cookie)||(m->io.read32(m->io.cookie,DEBUG)&MCLK_MASK)!=MCLK_VALUE){e=I9100_AP_IO;goto failed;}
 m->ready=1;goto done;
failed:
 m->fault=1;m->error=e;
done:
 m->io.unlock(m->io.cookie);return e;
}
int i9100_audio_mclk_rate(struct i9100_audio_mclk *m,uint32_t *hz)
{
 if(hz)*hz=0;if(!m||!hz)return I9100_AP_ARGUMENT;int e=valid(m->self,m,m->initialized);if(e)return e;
 if(m->io.lock(m->io.cookie))return I9100_AP_BUSY;
 if(!m->owned||!m->ready||m->fault)e=I9100_AP_DISABLED;
 else if(m->io.lease_valid(m->io.cookie)||(m->io.read32(m->io.cookie,DEBUG)&MCLK_MASK)!=MCLK_VALUE)
  {e=I9100_AP_IO;m->fault=1;m->ready=0;m->error=e;}
 else{e=0;*hz=m->verified_xtal_hz;}
 m->io.unlock(m->io.cookie);return e;
}
static int pin(const struct i9100_audio_pmu_io *io,const uint32_t *owned,const uint32_t *ready,const uint32_t *fault,uint32_t *holds,int add)
{
 if(io->lock(io->cookie))return I9100_AP_BUSY;int e=0;
 if(!*owned||(add&&(!*ready||*fault||*holds==UINT32_MAX))||(!add&&!*holds))e=I9100_AP_OWNER;
 else if(add)(*holds)++;else (*holds)--;
 io->unlock(io->cookie);return e;
}
int i9100_audio_mclk_hold(struct i9100_audio_mclk *m)
{if(!m)return I9100_AP_ARGUMENT;int e=valid(m->self,m,m->initialized);return e?e:pin(&m->io,&m->owned,&m->ready,&m->fault,&m->holds,1);}
int i9100_audio_mclk_unhold(struct i9100_audio_mclk *m)
{if(!m)return I9100_AP_ARGUMENT;int e=valid(m->self,m,m->initialized);return e?e:pin(&m->io,&m->owned,&m->ready,&m->fault,&m->holds,0);}
int i9100_audio_mclk_release(struct i9100_audio_mclk *m)
{
 if(!m)return I9100_AP_ARGUMENT;int e=valid(m->self,m,m->initialized);if(e)return e;
 if(m->io.lock(m->io.cookie))return I9100_AP_BUSY;
 if(!m->owned){e=I9100_AP_OWNER;goto done;}
 if(m->holds||m->io.consumers_idle(m->io.cookie)){e=I9100_AP_BUSY;goto done;}
 if(m->io.lease_valid(m->io.cookie)){e=I9100_AP_OWNER;goto done;}
 field(&m->io,DEBUG,MCLK_MASK,m->saved);
 if((m->io.read32(m->io.cookie,DEBUG)&MCLK_MASK)!=m->saved){e=I9100_AP_IO;m->fault=1;m->error=e;goto done;}
 m->owned=m->ready=m->fault=0;e=0;
done:
 m->io.unlock(m->io.cookie);return e;
}
int i9100_audio_domain_init(struct i9100_audio_domain *d,const struct i9100_audio_pmu_io *io)
{
 if(!d||d->initialized||!io_valid(io)||!io->temp_clocks_get||!io->temp_clocks_put||!io->confirm_bus)return I9100_AP_ARGUMENT;
 memset(d,0,sizeof(*d));d->io=*io;d->self=(uintptr_t)d;d->initialized=1;return 0;
}
int i9100_audio_domain_enable(struct i9100_audio_domain *d,int enabled)
{
 if(!d)return I9100_AP_ARGUMENT;int e=valid(d->self,d,d->initialized);if(e)return e;
 if(enabled!=0&&enabled!=1)return I9100_AP_ARGUMENT;if(d->io.lock(d->io.cookie))return I9100_AP_BUSY;
 e=d->owned||d->temp_owned||d->holds?I9100_AP_BUSY:0;if(!e)d->enabled=enabled;d->io.unlock(d->io.cookie);return e;
}
int i9100_audio_domain_acquire(struct i9100_audio_domain *d)
{
 if(!d)return I9100_AP_ARGUMENT;int e=valid(d->self,d,d->initialized);if(e)return e;
 if(d->io.lock(d->io.cookie))return I9100_AP_BUSY;
 if(!d->enabled){e=I9100_AP_DISABLED;goto done;}
 if(d->owned||d->temp_owned||d->fault||d->holds){e=I9100_AP_BUSY;goto done;}
 if(d->io.lease_valid(d->io.cookie)){e=I9100_AP_OWNER;goto done;}
 if(d->io.consumers_idle(d->io.cookie)){e=I9100_AP_BUSY;goto done;}
 uint32_t original=d->io.read32(d->io.cookie,CONFIG)&POWER_MASK;
 if((original!=0U&&original!=POWER_MASK)||(d->io.read32(d->io.cookie,STATUS)&POWER_MASK)!=original)
  {e=I9100_AP_BUSY;goto done;}
 d->saved=original;d->owned=1;d->ready=0;d->error=0;d->temp_owned=1;
 if(d->io.temp_clocks_get(d->io.cookie)){e=I9100_AP_IO;goto failed;}
 if(original!=POWER_MASK)field(&d->io,CONFIG,POWER_MASK,POWER_MASK);
 if((d->io.read32(d->io.cookie,CONFIG)&POWER_MASK)!=POWER_MASK){e=I9100_AP_IO;goto failed;}
 e=status(&d->io,POWER_MASK);if(e)goto failed;
 if(d->io.confirm_bus(d->io.cookie)){e=I9100_AP_IO;goto failed;}
 if(d->io.temp_clocks_put(d->io.cookie)){e=I9100_AP_IO;goto failed;}
 d->temp_owned=0;
 if(d->io.lease_valid(d->io.cookie)||(d->io.read32(d->io.cookie,STATUS)&POWER_MASK)!=POWER_MASK){e=I9100_AP_IO;goto failed;}
 d->ready=1;goto done;
failed:
 d->fault=1;d->error=e;
done:
 d->io.unlock(d->io.cookie);return e;
}
int i9100_audio_domain_hold(struct i9100_audio_domain *d)
{if(!d)return I9100_AP_ARGUMENT;int e=valid(d->self,d,d->initialized);return e?e:pin(&d->io,&d->owned,&d->ready,&d->fault,&d->holds,1);}
int i9100_audio_domain_unhold(struct i9100_audio_domain *d)
{if(!d)return I9100_AP_ARGUMENT;int e=valid(d->self,d,d->initialized);return e?e:pin(&d->io,&d->owned,&d->ready,&d->fault,&d->holds,0);}
int i9100_audio_domain_release(struct i9100_audio_domain *d)
{
 if(!d)return I9100_AP_ARGUMENT;int e=valid(d->self,d,d->initialized);if(e)return e;
 if(d->io.lock(d->io.cookie))return I9100_AP_BUSY;
 if(!d->owned){e=I9100_AP_OWNER;goto done;}
 if(d->holds||d->io.consumers_idle(d->io.cookie)){e=I9100_AP_BUSY;goto done;}
 if(d->io.lease_valid(d->io.cookie)){e=I9100_AP_OWNER;goto done;}
 if(!d->temp_owned){d->temp_owned=1;if(d->io.temp_clocks_get(d->io.cookie)){e=I9100_AP_IO;goto failed;}}
 field(&d->io,CONFIG,POWER_MASK,d->saved);
 if((d->io.read32(d->io.cookie,CONFIG)&POWER_MASK)!=d->saved){e=I9100_AP_IO;goto failed;}
 e=status(&d->io,d->saved);if(e)goto failed;
 if(d->io.temp_clocks_put(d->io.cookie)){e=I9100_AP_IO;goto failed;}
 d->owned=d->ready=d->fault=d->temp_owned=0;e=0;goto done;
failed:
 d->fault=1;d->error=e;
done:
 d->io.unlock(d->io.cookie);return e;
}
