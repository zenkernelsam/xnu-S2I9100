/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mc1n2.h"
#include <string.h>
static int cold_off_locked(struct i9100_mc1n2 *);
static int clock_off_locked(struct i9100_mc1n2 *);
static int transfer(struct i9100_mc1n2 *c,struct i9100_i2c_message *m,unsigned count)
{unsigned done=0;int e=c->io.transfer(c->io.cookie,m,count,&done);return e||done!=count?I9100_MC_IO:0;}
static int direct_write(struct i9100_mc1n2 *c,uint8_t reg,uint8_t value)
{
 uint8_t bytes[2]={(uint8_t)(reg<<1),value};struct i9100_i2c_message m={0x3a,0,2,bytes};return transfer(c,&m,1);
}
static int direct_read(struct i9100_mc1n2 *c,uint8_t reg,uint8_t *value)
{
 uint8_t index=(uint8_t)(reg<<1),data=0;struct i9100_i2c_message m[2]={{0x3a,0,1,&index},{0x3a,1,1,&data}};
 int e=transfer(c,m,2);if(!e)*value=data;return e;
}
static uint8_t address(enum i9100_mc1n2_bank bank)
{const uint8_t ports[]={0,1,12,14,16};return ports[bank];}
static int read_bank(struct i9100_mc1n2 *c,enum i9100_mc1n2_bank bank,uint8_t reg,uint8_t *value)
{
 if(bank==I9100_MC_DIRECT)return direct_read(c,reg,value);
 int e=direct_write(c,address(bank),reg);return e?e:direct_read(c,(uint8_t)(address(bank)+1U),value);
}
static int write_bank(struct i9100_mc1n2 *c,enum i9100_mc1n2_bank bank,uint8_t reg,uint8_t value)
{
 if(bank==I9100_MC_DIRECT)return direct_write(c,reg,value);
 /* Two address/data pairs in one non-burst transaction. No window auto-
  * increment assumption and no bank cache that could survive a failed IO. */
 uint8_t port=address(bank),bytes[4]={(uint8_t)(port<<1),reg,(uint8_t)((port+1U)<<1),value};
 struct i9100_i2c_message m={0x3a,0,4,bytes};return transfer(c,&m,1);
}
static int mute_locked(struct i9100_mc1n2 *c)
{
 int e=0;uint8_t dac=0,power=0;
 /* Continue independent mute actions even if one bus transaction fails. */
 int r=read_bank(c,I9100_MC_CODEC,78,&dac);
 if(r){e=I9100_MC_IO;dac=0;} /* unknown DAC state: disable DAC as well as mute */
 r=write_bank(c,I9100_MC_CODEC,78,dac|2U);if(r)e=I9100_MC_IO;
 const uint8_t regs[]={35,36,37,38};
 for(unsigned n=0;n<4;n++){r=write_bank(c,I9100_MC_ANALOG,regs[n],n%2U?0U:0x60U);if(r)e=I9100_MC_IO;}
 r=write_bank(c,I9100_MC_MIXER,89,0x80);if(r)e=I9100_MC_IO;
 r=write_bank(c,I9100_MC_MIXER,90,0);if(r)e=I9100_MC_IO;
 r=read_bank(c,I9100_MC_ANALOG,3,&power);
 if(r){e=I9100_MC_IO;power=0xffU;} /* never enable a power field after a failed read */
 r=write_bank(c,I9100_MC_ANALOG,3,power|0x3fU);if(r)e=I9100_MC_IO;
 uint64_t started=c->io.now_ns(c->io.cookie);
 for(unsigned attempt=0;attempt<200U;attempt++){
  uint8_t left=0,right=0;r=read_bank(c,I9100_MC_ANALOG,12,&left);
  if(!r)r=read_bank(c,I9100_MC_ANALOG,13,&right);
  if(r){e=I9100_MC_IO;break;}
  if(!((left|right)&0x18U))break;
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(attempt==199U||now<started||now-started>20000000ULL){e=I9100_MC_TIMEOUT;break;}
  if(c->io.delay_us(c->io.cookie,100)){e=I9100_MC_IO;break;}
 }
 for(unsigned n=0;n<4;n++){
  uint8_t value=0xff;r=read_bank(c,I9100_MC_ANALOG,regs[n],&value);
  if(r||(value&31U))e=I9100_MC_IO;
 }
 uint8_t verify=0;
 r=read_bank(c,I9100_MC_ANALOG,3,&verify);if(r||(verify&0x3fU)!=0x3fU)e=I9100_MC_IO;
 r=read_bank(c,I9100_MC_CODEC,78,&verify);if(r||!(verify&2U))e=I9100_MC_IO;
 c->muted=e?0U:1U;c->fault=e?1U:0U;c->gain_armed=0;c->analog_gain=0;c->actual_gain_mdb=INT32_MIN;
 return e;
}
int i9100_mc1n2_init(struct i9100_mc1n2 *c,const struct i9100_peripheral_i2c *io)
{
 if(!c||c->initialized||!io||!io->transfer||!io->claim||!io->release||!io->now_ns||!io->delay_us)return I9100_MC_ARGUMENT;
 memset(c,0,sizeof(*c));c->io=*io;c->initialized=1;c->actual_gain_mdb=INT32_MIN;return 0;
}
int i9100_mc1n2_enable(struct i9100_mc1n2 *c,int enabled)
{
 if(!c||!c->initialized||(enabled!=0&&enabled!=1))return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=0;
 if(enabled){
  if(c->enabled||c->fault){e=I9100_MC_BUSY;goto done;}
  e=direct_read(c,8,&c->id);if(e)goto done;
  if(c->id!=0x78U&&c->id!=0x79U){e=I9100_MC_ID;goto done;}
  /* This explicit exclusive diagnostic takeover may only mute verified MC. */
  c->enabled=1;e=mute_locked(c);
 }else if(!c->enabled)e=I9100_MC_DISABLED;
 else {e=c->cold_owned?cold_off_locked(c):mute_locked(c);if(!e)c->enabled=0;}
done:
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_mute(struct i9100_mc1n2 *c)
{
 if(!c||!c->initialized)return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=c->enabled?mute_locked(c):I9100_MC_DISABLED;c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_safe_gain(int32_t requested,uint8_t *code,int32_t *actual)
{
 if(!code||!actual||requested>-30000||requested<-96000)return I9100_MC_ARGUMENT;
 *code=requested>=-36000?1U:0U;*actual=*code?-36000:INT32_MIN;return 0;
}
int i9100_mc1n2_stage_gain(struct i9100_mc1n2 *c,enum i9100_mc1n2_output output,int32_t requested)
{
 uint8_t code=0;int32_t actual=0;
 if(!c||!c->initialized||(output!=I9100_MC_HEADPHONES&&output!=I9100_MC_SPEAKER))return I9100_MC_ARGUMENT;
 int e=i9100_mc1n2_safe_gain(requested,&code,&actual);if(e)return e;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 if(!c->enabled){e=I9100_MC_DISABLED;goto done;}
 if(c->fault){e=I9100_MC_IO;goto done;}
 e=mute_locked(c);if(e)goto done;
 uint8_t old=0;
 e=read_bank(c,I9100_MC_ANALOG,77,&old);
 if(!e)e=write_bank(c,I9100_MC_ANALOG,77,old&~3U); /* HP boost=0dB */
 uint8_t reg=output==I9100_MC_HEADPHONES?35U:37U;
 if(!e)e=write_bank(c,I9100_MC_ANALOG,reg,0x60U|code);
 if(!e)e=write_bank(c,I9100_MC_ANALOG,reg+1U,code);
 uint8_t left=0,right=0,power=0,boost=255;
 if(!e)e=read_bank(c,I9100_MC_ANALOG,reg,&left);
 if(!e)e=read_bank(c,I9100_MC_ANALOG,reg+1U,&right);
 if(!e)e=read_bank(c,I9100_MC_ANALOG,3,&power);
 if(!e)e=read_bank(c,I9100_MC_ANALOG,77,&boost);
 if(!e&&((left&31U)!=code||(right&31U)!=code||(power&0x3fU)!=0x3fU||(boost&3U)))e=I9100_MC_IO;
 if(e){int off=mute_locked(c);c->fault=1;if(off)c->muted=0;}
 else {c->analog_gain=code;c->actual_gain_mdb=actual;c->gain_armed=1;}
done:
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_read(struct i9100_mc1n2 *c,enum i9100_mc1n2_bank bank,uint8_t reg,uint8_t *value)
{
 if(!c||!c->initialized||!value||bank>I9100_MC_MIXER||bank<I9100_MC_DIRECT||(bank==I9100_MC_DIRECT&&reg>25U))return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 uint8_t data=0;int e=c->enabled?read_bank(c,bank,reg,&data):I9100_MC_DISABLED;
 if(!e)*value=data;c->io.release(c->io.cookie);return e;
}
static int checked_write(struct i9100_mc1n2 *c,enum i9100_mc1n2_bank bank,uint8_t reg,uint8_t value,uint8_t mask)
{
 int e=write_bank(c,bank,reg,value);uint8_t actual=0;
 if(!e)e=read_bank(c,bank,reg,&actual);
 if(!e&&(actual&mask)!=(value&mask))e=I9100_MC_IO;return e;
}
static int pause(struct i9100_mc1n2 *c,uint32_t us)
{return c->io.delay_us(c->io.cookie,us)?I9100_MC_IO:0;}
static int ready(struct i9100_mc1n2 *c,uint8_t bits)
{
 uint64_t started=c->io.now_ns(c->io.cookie);
 for(unsigned n=0;n<200U;n++){
  uint8_t value=0;int e=read_bank(c,I9100_MC_ANALOG,47,&value);if(e)return e;
  if((value&bits)==bits)return 0;
  uint64_t now=c->io.now_ns(c->io.cookie);
  if(now<started||now-started>20000000ULL)break;
  if(pause(c,100))return I9100_MC_IO;
 }
 return I9100_MC_TIMEOUT;
}
static int cold_off_locked(struct i9100_mc1n2 *c)
{
 if(c->clock_owned){int off=clock_off_locked(c);c->clock_cleanup_error=off;if(off)return off;}
 int e=mute_locked(c),r;
 r=checked_write(c,I9100_MC_MIXER,137,0,0xff);if(r)e=r;
 r=checked_write(c,I9100_MC_BASE,12,0xae,0xff);if(r)e=r;
 r=checked_write(c,I9100_MC_ANALOG,2,0x0f,0x0f);if(r)e=r;
 r=checked_write(c,I9100_MC_CODEC,15,1,1);if(r)e=r;
 r=checked_write(c,I9100_MC_BASE,1,6,6);if(r)e=r;
 r=checked_write(c,I9100_MC_BASE,3,0x1f,0x1f);if(r)e=r;
 if(e)c->fault=1;else{c->cold_owned=0;c->cold_stage=0;c->fault=0;}
 return e;
}
int i9100_mc1n2_cold_stop(struct i9100_mc1n2 *c)
{
 if(!c||!c->initialized)return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=!c->enabled||!c->cold_owned?I9100_MC_DISABLED:cold_off_locked(c);
 c->io.release(c->io.cookie);return e;
}
static int clock_off_locked(struct i9100_mc1n2 *c)
{
 if(!c->clock_guard.lease_valid||!c->clock_guard.consumers_idle)return I9100_MC_ARGUMENT;
 if(c->clock_guard.consumers_idle(c->clock_guard.cookie))return I9100_MC_BUSY;
 int e=c->clock_guard.lease_valid(c->clock_guard.cookie)?I9100_MC_NOT_READY:0,r;
 r=mute_locked(c);if(r)e=r;
 /* Mask output pads before stopping their timer, while AP/DMA are idle. */
 r=checked_write(c,I9100_MC_BASE,12,c->clock_saved_pad,0xff);
 if(r)e=r;else c->clock_active=0;
 r=checked_write(c,I9100_MC_MIXER,137,c->clock_saved_start,0xff);if(r)e=r;
 r=checked_write(c,I9100_MC_MIXER,135,c->clock_saved_fs,0xff);if(r)e=r;
 if(e)c->fault=1;
 else{c->clock_owned=c->clock_active=c->clock_stage=0;c->fault=0;memset(&c->clock_guard,0,sizeof(c->clock_guard));}
 return e;
}
int i9100_mc1n2_clock_stop(struct i9100_mc1n2 *c)
{
 if(!c||!c->initialized)return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=!c->enabled||!c->clock_owned?I9100_MC_DISABLED:clock_off_locked(c);
 c->clock_cleanup_error=e;if(e&&!c->clock_error)c->clock_error=e;
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_clock_start(struct i9100_mc1n2 *c,const struct i9100_mc1n2_clock_guard *g)
{
 if(!c||!c->initialized||!g||!g->lease_valid||!g->consumers_idle)return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=0;uint8_t identity=0,pad=0,start=0,fs=0,pll=0;
 if(!c->enabled||!c->cold_owned||c->cold_stage!=5U){e=I9100_MC_DISABLED;goto done;}
 if(c->clock_owned||c->fault){e=I9100_MC_BUSY;goto done;}
 if(c->mclk_hz!=24000000U||(c->sample_rate!=44100U&&c->sample_rate!=48000U))
  {e=I9100_MC_ARGUMENT;goto done;}
 if(g->lease_valid(g->cookie)){e=I9100_MC_NOT_READY;goto done;}
 if(g->consumers_idle(g->cookie)){e=I9100_MC_BUSY;goto done;}
 c->clock_error=c->clock_cleanup_error=0;c->clock_failure_stage=0;
 e=direct_read(c,8,&identity);if(!e&&identity!=0x79U)e=I9100_MC_ID;
 if(!e)e=read_bank(c,I9100_MC_BASE,12,&pad);
 if(!e)e=read_bank(c,I9100_MC_MIXER,137,&start);
 if(!e)e=read_bank(c,I9100_MC_MIXER,135,&fs);
 if(!e)e=read_bank(c,I9100_MC_CODEC,15,&pll);
 if(!e&&((pad&0xf0U)!=0xa0U||start||fs!=(uint8_t)(c->sample_rate==48000U?0xa0U:0xa1U)||(pll&1U)))e=I9100_MC_NOT_READY;
 if(!e)e=ready(c,0xc0U);
 if(e){c->clock_error=e;goto done;}
 e=mute_locked(c);if(e){c->clock_error=e;goto done;}
 c->clock_guard=*g;c->clock_saved_pad=pad;c->clock_saved_start=start;c->clock_saved_fs=fs;
 c->clock_owned=1;c->clock_stage=1;
 /* Auto-FS is a slave-mode option. Master uses explicit FS and 32fs. */
 e=checked_write(c,I9100_MC_MIXER,135,c->sample_rate==48000U?0x20U:0x21U,0xff);if(e)goto failed;
 c->clock_stage=2;
 /* Vendor MCB_DITIM0_START is bit6 (40), not bit4. DIT remains off. */
 e=checked_write(c,I9100_MC_MIXER,137,0x4cU,0xff);if(e)goto failed;
 c->clock_stage=3;
 e=checked_write(c,I9100_MC_BASE,12,(pad&0x0fU)|0x50U,0xff);if(e)goto failed;
 if(g->lease_valid(g->cookie)){e=I9100_MC_NOT_READY;goto failed;}
 c->clock_stage=4;c->clock_active=1;goto done;
failed:
 c->clock_error=e;c->clock_failure_stage=c->clock_stage;c->clock_cleanup_error=clock_off_locked(c);c->fault=1;
done:
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_cold_init(struct i9100_mc1n2 *c,uint32_t mclk,uint32_t rate,int verified)
{
 if(!c||!c->initialized||verified!=1||mclk!=24000000U||(rate!=44100U&&rate!=48000U))return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=0;uint8_t identity=0;
 if(!c->enabled){e=I9100_MC_DISABLED;goto done;}
 if(c->fault||c->cold_owned){e=I9100_MC_BUSY;goto done;}
 e=direct_read(c,8,&identity);if(e)goto done;
 if(identity!=0x79U){e=I9100_MC_ID;goto done;}
 c->cold_owned=1;c->cold_stage=1;c->mclk_hz=mclk;c->sample_rate=rate;c->cold_error=0;c->failure_stage=0;
 /* Vendor AB reset ordering. Both digital and analog reset defaults power
  * down the output stages. Re-assert explicit mute immediately afterwards. */
 e=direct_write(c,0,1);if(!e)e=direct_write(c,0,0);
 if(!e)e=write_bank(c,I9100_MC_ANALOG,0,1);if(!e)e=write_bank(c,I9100_MC_ANALOG,0,0);
 if(!e)e=mute_locked(c);if(e)goto failed;
 const struct setting {uint8_t bank,reg,value;} pins[]={
  {I9100_MC_BASE,9,0x88},{I9100_MC_BASE,10,0x80},{I9100_MC_BASE,11,0xaa},
  {I9100_MC_BASE,12,0xae},{I9100_MC_BASE,13,0},{I9100_MC_BASE,26,0x88}
 };
 for(unsigned n=0;n<sizeof(pins)/sizeof(pins[0])&&!e;n++)e=checked_write(c,pins[n].bank,pins[n].reg,pins[n].value,0xff);
 c->cold_stage=2;
 if(!e)e=checked_write(c,I9100_MC_CODEC,15,1,1);
 if(!e)e=checked_write(c,I9100_MC_CODEC,4,0,0xc0); /* CMOS clock source */
 for(unsigned n=16;n<=19U&&!e;n++)e=checked_write(c,I9100_MC_CODEC,(uint8_t)n,(n&1U)?86U:28U,0xff);
 if(!e)e=checked_write(c,I9100_MC_CODEC,1,0x18,0xff);
 if(!e)e=checked_write(c,I9100_MC_CODEC,15,0,1);if(!e)e=pause(c,2000);
 if(!e)e=checked_write(c,I9100_MC_BASE,1,0,6);if(!e)e=checked_write(c,I9100_MC_BASE,0,0,0x10);
 if(!e)e=checked_write(c,I9100_MC_BASE,3,0x1e,0x1f);
 if(!e)e=checked_write(c,I9100_MC_CODEC,79,0,0xff); /* DCL boost0dB, not vendor12dB */
 if(e)goto failed;
 c->cold_stage=3;
 if(!e)e=checked_write(c,I9100_MC_ANALOG,24,0,0xff);
 if(!e)e=checked_write(c,I9100_MC_ANALOG,44,0x30,0x33); /* speaker high-Z, amps remain off */
 if(!e)e=checked_write(c,I9100_MC_ANALOG,45,0,0xff);if(!e)e=checked_write(c,I9100_MC_ANALOG,46,0,0xff);
 if(!e)e=checked_write(c,I9100_MC_ANALOG,77,0,3); /* HP boost0dB */
 if(!e)e=checked_write(c,I9100_MC_ANALOG,79,0x24,0xff);
 if(!e)e=checked_write(c,I9100_MC_ANALOG,16,7,7); /* manual analog power control */
 if(!e)e=checked_write(c,I9100_MC_ANALOG,2,7,15);if(!e)e=pause(c,1000);if(!e)e=ready(c,0x80);
 if(!e)e=checked_write(c,I9100_MC_ANALOG,2,6,15);if(!e)e=pause(c,15000);if(!e)e=ready(c,0xc0);
 if(e)goto failed;
 c->cold_stage=4;
 uint16_t dir_rate=rate==48000U?32768U:30106U,dit_rate=rate==48000U?4096U:4458U;
 const struct setting dio[]={
  {I9100_MC_MIXER,130,0},{I9100_MC_MIXER,131,(uint8_t)(dir_rate>>8)},
  {I9100_MC_MIXER,132,(uint8_t)dir_rate},{I9100_MC_MIXER,133,(uint8_t)(dit_rate>>8)},
  {I9100_MC_MIXER,134,(uint8_t)dit_rate},{I9100_MC_MIXER,135,(uint8_t)(rate==48000U?0xa0U:0xa1U)},
  {I9100_MC_MIXER,136,0},{I9100_MC_MIXER,137,0},{I9100_MC_MIXER,142,0x44},
  {I9100_MC_MIXER,143,0x10},{I9100_MC_MIXER,144,0x10},{I9100_MC_MIXER,110,0x30}
 };
 for(unsigned n=0;n<sizeof(dio)/sizeof(dio[0])&&!e;n++)e=checked_write(c,dio[n].bank,dio[n].reg,dio[n].value,0xff);
 if(!e)e=checked_write(c,I9100_MC_BASE,3,0x1c,0x1f); /* DIR0 digital power, no start */
 if(!e)e=checked_write(c,I9100_MC_CODEC,78,2,3); /* DAC remains off/muted */
 if(!e)e=mute_locked(c);if(e)goto failed;
 c->cold_stage=5;goto done;
failed:
 c->failure_stage=c->cold_stage;c->cold_error=e;
 (void)cold_off_locked(c);c->fault=1;
done:
 c->io.release(c->io.cookie);return e;
}
