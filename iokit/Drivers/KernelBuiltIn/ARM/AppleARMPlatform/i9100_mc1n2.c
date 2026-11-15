/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mc1n2.h"
#include <string.h>
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
 else {e=mute_locked(c);if(!e)c->enabled=0;}
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
