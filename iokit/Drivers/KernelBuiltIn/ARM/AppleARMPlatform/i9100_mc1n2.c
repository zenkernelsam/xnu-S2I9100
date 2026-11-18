/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mc1n2.h"
#include <string.h>
static int cold_off_locked(struct i9100_mc1n2 *);
static int clock_off_locked(struct i9100_mc1n2 *);
static int output_silence_locked(struct i9100_mc1n2 *);
static int valid(struct i9100_mc1n2 *c)
{return !c||!c->initialized?I9100_MC_ARGUMENT:c->self!=(uintptr_t)c?I9100_MC_OWNER:0;}
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
 memset(c,0,sizeof(*c));c->io=*io;c->self=(uintptr_t)c;c->initialized=1;c->actual_gain_mdb=INT32_MIN;return 0;
}
int i9100_mc1n2_enable(struct i9100_mc1n2 *c,int enabled)
{
 int check=valid(c);if(check)return check;if(enabled!=0&&enabled!=1)return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=0;
 if(enabled){
  if(c->enabled||c->fault){e=I9100_MC_BUSY;goto done;}
  e=direct_read(c,8,&c->id);if(e)goto done;
  if(c->id!=0x78U&&c->id!=0x79U){e=I9100_MC_ID;goto done;}
  /* This explicit exclusive diagnostic takeover may only mute verified MC. */
  c->enabled=1;e=mute_locked(c);
 }else if(!c->enabled)e=I9100_MC_DISABLED;
 else if(c->output_owner)e=I9100_MC_BUSY;
 else {e=c->cold_owned?cold_off_locked(c):mute_locked(c);if(!e)c->enabled=0;}
done:
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_mute(struct i9100_mc1n2 *c)
{
 int check=valid(c);if(check)return check;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=!c->enabled?I9100_MC_DISABLED:c->output_owner?output_silence_locked(c):mute_locked(c);
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_safe_gain(int32_t requested,uint8_t *code,int32_t *actual)
{
 if(!code||!actual||requested>-30000||requested<-96000)return I9100_MC_ARGUMENT;
 *code=requested>=-36000?1U:0U;*actual=*code?-36000:INT32_MIN;return 0;
}
int i9100_mc1n2_stage_gain(struct i9100_mc1n2 *c,enum i9100_mc1n2_output output,int32_t requested)
{
 uint8_t code=0;int32_t actual=0;
 int check=valid(c);if(check)return check;
 if(output!=I9100_MC_HEADPHONES&&output!=I9100_MC_SPEAKER)return I9100_MC_ARGUMENT;
 int e=i9100_mc1n2_safe_gain(requested,&code,&actual);if(e)return e;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 if(!c->enabled){e=I9100_MC_DISABLED;goto done;}
 if(c->output_owner){e=I9100_MC_BUSY;goto done;}
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
 int check=valid(c);if(check)return check;
 if(!value||bank>I9100_MC_MIXER||bank<I9100_MC_DIRECT||(bank==I9100_MC_DIRECT&&reg>25U))return I9100_MC_ARGUMENT;
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
static int output_bits(struct i9100_mc1n2 *c,enum i9100_mc1n2_bank bank,uint8_t reg,uint8_t value,uint8_t mask)
{
 uint8_t actual=0;int e=read_bank(c,bank,reg,&actual);
 return e?e:(actual&mask)!=(value&mask)?I9100_MC_IO:0;
}
static int output_wait(struct i9100_mc1n2 *c,uint32_t us)
{
 uint64_t before=c->io.now_ns(c->io.cookie);int e=pause(c,us);if(e)return e;
 uint64_t after=c->io.now_ns(c->io.cookie);
 /* A successful delay callback alone is not evidence that a power interval
  * elapsed. Refuse frozen/backwards clocks rather than enabling stage two. */
 return after<before||after-before<(uint64_t)us*1000U?I9100_MC_TIMEOUT:0;
}
static int output_flags_clear(struct i9100_mc1n2 *c,enum i9100_mc1n2_bank bank,uint8_t left,uint8_t right,uint8_t mask)
{
 uint64_t before=c->io.now_ns(c->io.cookie);
 for(unsigned n=0;n<200U;n++){
  uint8_t l=0,r=0;int e=read_bank(c,bank,left,&l);if(!e)e=read_bank(c,bank,right,&r);if(e)return e;
  if(!((l|r)&mask))return 0;
  uint64_t at=c->io.now_ns(c->io.cookie);
  if(at<before||at-before>=50000000ULL)return I9100_MC_TIMEOUT;
  e=output_wait(c,250);if(e)return e;
 }
 return I9100_MC_TIMEOUT;
}
static uint8_t output_da_bits(struct i9100_mc1n2 *c)
{return c->id==0x78U?0x08U:0x18U;}
static uint8_t output_amp_bits(struct i9100_mc1n2 *c)
{return c->output_kind==I9100_MC_HEADPHONES?0x30U:0x0fU;}
static int output_silence_locked(struct i9100_mc1n2 *c)
{
 int e=0,r;uint8_t dac=0;
 r=read_bank(c,I9100_MC_CODEC,78,&dac);if(r){e=r;dac=0;}
 r=checked_write(c,I9100_MC_CODEC,78,dac|2U,3U);if(r)e=r;
 for(unsigned n=0;n<4U;n++){
  r=checked_write(c,I9100_MC_ANALOG,(uint8_t)(35U+n),(n&1U)?0U:0x60U,(n&1U)?31U:0x3fU);if(r)e=r;
 }
 r=checked_write(c,I9100_MC_MIXER,89,0x80U,0x7fU);if(r)e=r;
 r=checked_write(c,I9100_MC_MIXER,90,0,0x7fU);if(r)e=r;
 r=output_flags_clear(c,I9100_MC_ANALOG,12,13,0x18U);if(r)e=r;
 r=output_flags_clear(c,I9100_MC_MIXER,10,11,2U);if(r)e=r;
 c->muted=e?0U:1U;c->output_running=0;c->gain_armed=0;c->analog_gain=0;c->actual_gain_mdb=INT32_MIN;
 if(!e&&c->output_powered&&c->output_stage==7U)c->output_stage=5;
 if(e){c->fault=1;if(!c->output_error)c->output_error=e;}
 /* Do not power off amps/CP or discard their owner here: PCM uses this
  * callback both before start and on faults while DMA can still be live. */
 return e;
}
static int output_lease_check(struct i9100_mc1n2_output_lease *l)
{
 if(!l||!l->owned||l->self!=(uintptr_t)l||!l->codec)return I9100_MC_OWNER;
 int e=valid(l->codec);if(e)return e;
 return l->codec->output_owner!=l||l->codec->output_generation!=l->generation?I9100_MC_OWNER:0;
}
static int output_live(struct i9100_mc1n2 *c,int require_idle)
{
 if(c->output_guard.lease_valid(c->output_guard.cookie))return I9100_MC_NOT_READY;
 return require_idle&&c->output_guard.consumers_idle(c->output_guard.cookie)?I9100_MC_BUSY:0;
}
static int output_off_locked(struct i9100_mc1n2 *c)
{
 c->output_stage=8;int e=output_silence_locked(c),r;
 /* Independent fail-safe power-down actions still run after a mute IO
  * failure. Never restore an audible gain or turn off clocks on that error. */
 r=checked_write(c,I9100_MC_ANALOG,44,c->output_saved_mode|0x30U,0x33U);if(r)e=r;
 r=checked_write(c,I9100_MC_ANALOG,3,c->output_saved_power[1]|0x3fU,0xffU);if(r)e=r;
 else c->output_powered=0;
 r=output_flags_clear(c,I9100_MC_ANALOG,12,13,0x18U);if(r)e=r;
 if(e)return e;
 if(c->output_guard.consumers_idle(c->output_guard.cookie))return I9100_MC_BUSY;
 /* AA has one AP_DA bit; AB has separate AP_DAL/AP_DAR. The saved upper
  * fields are preserved instead of writing AB's reserved bit into AA. */
 e=checked_write(c,I9100_MC_ANALOG,5,c->output_saved_power[2]|output_da_bits(c),0xffU);if(e)return e;
 e=output_flags_clear(c,I9100_MC_MIXER,10,11,2U);if(e)return e;
 e=checked_write(c,I9100_MC_CODEC,78,2,3);if(e)return e;
 uint8_t power=0;e=read_bank(c,I9100_MC_ANALOG,2,&power);if(e)return e;
 if(!(power&2U)){
  e=ready(c,2U);if(e)return e; /* vendor manual CPPDRDY before CP down */
  e=checked_write(c,I9100_MC_ANALOG,2,power|2U,0x0fU);if(e)return e;
 }
 for(unsigned n=0;n<7U;n++){
  e=checked_write(c,I9100_MC_ANALOG,(uint8_t)(61U+n),c->output_saved_routes[n],0xffU);if(e)return e;
 }
 e=checked_write(c,I9100_MC_MIXER,110,c->output_saved_source,0xffU);if(e)return e;
 e=checked_write(c,I9100_MC_MIXER,111,c->output_saved_swap,0xffU);if(e)return e;
 e=checked_write(c,I9100_MC_ANALOG,44,c->output_saved_mode,0x33U);if(e)return e;
 e=checked_write(c,I9100_MC_ANALOG,2,c->output_saved_power[0],0x0fU);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,5,c->output_saved_power[2],0xffU);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,3,c->output_saved_power[1],0xffU);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,16,7,7);if(e)return e;
 struct i9100_mc1n2_output_lease *l=c->output_owner;
 l->owned=0;c->output_owner=0;c->output_powered=c->output_running=c->output_stage=c->output_kind=0;
 c->output_gain_code=0;memset(&c->output_guard,0,sizeof(c->output_guard));c->fault=0;
 return 0;
}
static int output_power_check(struct i9100_mc1n2 *c,int silent)
{
 int e=output_live(c,0);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,16,7,7);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,2,c->output_saved_power[0]&
  (c->output_kind==I9100_MC_HEADPHONES?(uint8_t)~2U:0xffU),0x0fU);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,3,c->output_saved_power[1]&~output_amp_bits(c),0xffU);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,5,c->output_saved_power[2]&~output_da_bits(c),0xffU);if(e)return e;
 e=output_bits(c,I9100_MC_ANALOG,77,0,3);if(e)return e;
 e=output_bits(c,I9100_MC_CODEC,79,0,0xffU);if(e)return e; /* no DCL boost */
 e=output_bits(c,I9100_MC_CODEC,78,1,1);if(e)return e;
 e=output_bits(c,I9100_MC_MIXER,110,0x30U,0xffU);if(e)return e;
 e=output_bits(c,I9100_MC_MIXER,111,0,0xffU);if(e)return e;
 uint8_t routes[7]={0,0,0,0,0,0,0};
 if(c->output_kind==I9100_MC_HEADPHONES)routes[0]=routes[2]=0x40U;
 else routes[3]=routes[4]=routes[5]=routes[6]=0x40U;
 for(unsigned n=0;n<7U;n++){e=output_bits(c,I9100_MC_ANALOG,(uint8_t)(61U+n),routes[n],0xffU);if(e)return e;}
 e=output_bits(c,I9100_MC_ANALOG,44,c->output_kind==I9100_MC_HEADPHONES?c->output_saved_mode:2U,0x33U);if(e)return e;
 if(silent){
  for(unsigned n=0;n<4U;n++){e=output_bits(c,I9100_MC_ANALOG,(uint8_t)(35U+n),0,31U);if(e)return e;}
  e=output_bits(c,I9100_MC_MIXER,89,0,0x7fU);if(e)return e;
  e=output_bits(c,I9100_MC_MIXER,90,0,0x7fU);if(e)return e;
 }
 return ready(c,0xc0U);
}
int i9100_mc1n2_output_prepare(int enabled,struct i9100_mc1n2 *c,
 enum i9100_mc1n2_output kind,int32_t requested,const struct i9100_mc1n2_output_guard *g,
 struct i9100_mc1n2_output_lease *l)
{
 if(enabled!=1)return I9100_MC_DISABLED;
 int e=valid(c);if(e)return e;uint8_t gain=0;int32_t actual=0;
 if(!l||l->owned||!g||!g->lease_valid||!g->consumers_idle||!g->stream_ready||
    (kind!=I9100_MC_HEADPHONES&&kind!=I9100_MC_SPEAKER))return I9100_MC_ARGUMENT;
 e=i9100_mc1n2_safe_gain(requested,&gain,&actual);if(e)return e;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 uint8_t power[3]={0},routes[7]={0},source=0,swap=0,mode=0,id=0;
 if(!c->enabled||!c->cold_owned||c->cold_stage!=5U||!c->clock_owned||!c->clock_active||c->clock_stage!=4U)
  {e=I9100_MC_DISABLED;goto done;}
 if(c->fault||c->output_owner){e=I9100_MC_BUSY;goto done;}
 if(c->output_generation==UINT32_MAX){e=I9100_MC_OWNER;goto done;}
 if(g->lease_valid(g->cookie)){e=I9100_MC_NOT_READY;goto done;}
 if(g->consumers_idle(g->cookie)){e=I9100_MC_BUSY;goto done;}
 e=direct_read(c,8,&id);if(!e&&id!=c->id)e=I9100_MC_ID;if(e)goto done;
 e=read_bank(c,I9100_MC_ANALOG,2,&power[0]);if(!e)e=read_bank(c,I9100_MC_ANALOG,3,&power[1]);
 if(!e)e=read_bank(c,I9100_MC_ANALOG,5,&power[2]);if(!e)e=read_bank(c,I9100_MC_ANALOG,44,&mode);
 if(!e&&((power[0]&15U)!=6U||(power[1]&0x3fU)!=0x3fU||
          (power[2]&output_da_bits(c))!=output_da_bits(c)||(mode&0x30U)!=0x30U))e=I9100_MC_NOT_READY;
 if(!e)e=output_bits(c,I9100_MC_ANALOG,16,7,7);
 if(!e)e=output_bits(c,I9100_MC_CODEC,78,2,3);
 if(!e)e=output_bits(c,I9100_MC_ANALOG,77,0,3);
 if(!e)e=output_bits(c,I9100_MC_CODEC,79,0,0xffU);
 if(!e)e=output_bits(c,I9100_MC_MIXER,137,0x4cU,0xffU);
 if(!e)e=output_bits(c,I9100_MC_MIXER,135,c->sample_rate==48000U?0x20U:0x21U,0xffU);
 if(!e)e=output_bits(c,I9100_MC_BASE,12,0x5eU,0xffU);
 for(unsigned n=0;n<7U&&!e;n++)e=read_bank(c,I9100_MC_ANALOG,(uint8_t)(61U+n),&routes[n]);
 if(!e)e=read_bank(c,I9100_MC_MIXER,110,&source);if(!e)e=read_bank(c,I9100_MC_MIXER,111,&swap);
 if(e)goto done;
 c->output_guard=*g;c->output_kind=kind;c->output_gain_code=gain;
 memcpy(c->output_saved_power,power,sizeof(power));memcpy(c->output_saved_routes,routes,sizeof(routes));
 c->output_saved_source=source;c->output_saved_swap=swap;c->output_saved_mode=mode;
 c->output_generation++;c->output_owner=l;c->output_stage=1;
 c->output_error=c->output_cleanup_error=0;c->output_failure_stage=0;
 memset(l,0,sizeof(*l));l->self=(uintptr_t)l;l->codec=c;l->generation=c->output_generation;l->owned=1;
 e=mute_locked(c);if(e)goto failed;
 e=checked_write(c,I9100_MC_MIXER,110,0x30U,0xffU);if(!e)e=checked_write(c,I9100_MC_MIXER,111,0,0xffU);
 for(unsigned n=0;n<7U&&!e;n++){
  uint8_t route=kind==I9100_MC_HEADPHONES?(n==0U||n==2U?0x40U:0U):(n>=3U?0x40U:0U);
  e=checked_write(c,I9100_MC_ANALOG,(uint8_t)(61U+n),route,0xffU);
 }
 if(e)goto failed;c->output_stage=2;
 e=output_live(c,1);if(e)goto failed;
 /* The vendor 2->3->1 transition is executed at zero digital and analog
  * gains; safety does not depend on interpreting NSMUTE's name/polarity. */
 e=checked_write(c,I9100_MC_CODEC,78,2,3);if(!e)e=checked_write(c,I9100_MC_CODEC,78,3,3);
 if(!e)e=checked_write(c,I9100_MC_CODEC,78,1,3);
 if(!e)e=checked_write(c,I9100_MC_ANALOG,5,power[2]&~output_da_bits(c),0xffU);
 if(e)goto failed;c->output_stage=3;
 e=output_live(c,1);if(e)goto failed;
 if(kind==I9100_MC_HEADPHONES){
  e=checked_write(c,I9100_MC_ANALOG,2,power[0]&~2U,15U);if(!e)e=output_wait(c,300);
  if(!e)e=output_live(c,1);
  if(!e)e=checked_write(c,I9100_MC_ANALOG,3,power[1]&~0x30U,0xffU);
 }else{
  e=checked_write(c,I9100_MC_ANALOG,44,2U,0x33U);
  if(!e)e=checked_write(c,I9100_MC_ANALOG,3,power[1]&~0x05U,0xffU);
  if(!e)e=output_wait(c,150);if(!e)e=output_live(c,1);
  if(!e)e=checked_write(c,I9100_MC_ANALOG,3,power[1]&~0x0fU,0xffU);
 }
 if(e)goto failed;c->output_stage=4;
 e=output_wait(c,kind==I9100_MC_HEADPHONES?9000U:13000U);
 if(!e)e=output_flags_clear(c,I9100_MC_ANALOG,12,13,0x18U);
 if(!e)e=output_power_check(c,1);if(!e)e=output_live(c,1);if(e)goto failed;
 c->output_stage=5;c->output_powered=1;c->output_running=0;c->muted=1;goto done;
failed:
 c->output_error=e;c->output_failure_stage=c->output_stage;
 c->output_cleanup_error=output_off_locked(c);c->fault=1;
done:
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_output_activate(struct i9100_mc1n2_output_lease *l)
{
 int e=output_lease_check(l);if(e)return e;struct i9100_mc1n2 *c=l->codec;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 if(c->fault||c->output_running||!c->output_powered||c->output_stage!=5U){e=I9100_MC_BUSY;goto done;}
 if(c->output_gain_code!=1U){e=I9100_MC_NOT_READY;goto done;}
 e=output_live(c,0);if(e)goto done;
 if(c->output_guard.stream_ready(c->output_guard.cookie,c->sample_rate)){e=I9100_MC_NOT_READY;goto done;}
 e=output_power_check(c,1);if(e)goto failed;
 c->output_stage=6;c->muted=0;
 /* Actual vendor digital volume law: register96=0dB,66=-30dB,0=mute.
  * Keep analog at zero until the capped digital path is readback-verified. */
 e=checked_write(c,I9100_MC_CODEC,78,1,3);
 if(!e)e=checked_write(c,I9100_MC_MIXER,89,0x80U|66U,0x7fU);
 if(!e)e=checked_write(c,I9100_MC_MIXER,90,66U,0x7fU);
 uint8_t reg=c->output_kind==I9100_MC_HEADPHONES?35U:37U;
 if(!e)e=checked_write(c,I9100_MC_ANALOG,reg,0x61U,0x3fU);
 if(!e)e=checked_write(c,I9100_MC_ANALOG,reg+1U,1U,31U);
 if(!e)e=output_flags_clear(c,I9100_MC_ANALOG,12,13,0x18U);
 if(!e)e=output_live(c,0);
 if(!e&&c->output_guard.stream_ready(c->output_guard.cookie,c->sample_rate))e=I9100_MC_NOT_READY;
 if(e)goto failed;
 c->output_stage=7;c->output_running=1;c->gain_armed=1;c->analog_gain=1;c->actual_gain_mdb=-36000;goto done;
failed:
 c->output_error=e;c->output_failure_stage=c->output_stage;
 c->output_cleanup_error=output_silence_locked(c);c->output_stage=5;c->fault=1;
done:
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_output_stop(struct i9100_mc1n2_output_lease *l)
{
 int e=output_lease_check(l);if(e)return e;struct i9100_mc1n2 *c=l->codec;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 e=output_off_locked(c);c->output_cleanup_error=e;
 if(e){c->fault=1;if(!c->output_error)c->output_error=e;}
 c->io.release(c->io.cookie);return e;
}
static int cold_off_locked(struct i9100_mc1n2 *c)
{
 if(c->output_owner)return I9100_MC_BUSY;
 if(c->clock_owned){int off=clock_off_locked(c);c->clock_cleanup_error=off;if(off)return off;}
 int e=mute_locked(c),r;
 r=checked_write(c,I9100_MC_MIXER,137,0,0xff);if(r)e=r;
 r=checked_write(c,I9100_MC_BASE,12,0xae,0xff);if(r)e=r;
 r=checked_write(c,I9100_MC_ANALOG,2,0x0f,0x0f);if(r)e=r;
 if(c->id==0x78U){
  /* AA: DPDI/PDM and DPB are not written down in the same transaction. */
  r=checked_write(c,I9100_MC_BASE,3,0x1e,0x1f);if(r)e=r;
  r=checked_write(c,I9100_MC_BASE,3,0x1f,0x1f);if(r)e=r;
  r=checked_write(c,I9100_MC_BASE,1,6,7);if(r)e=r;
  r=checked_write(c,I9100_MC_BASE,14,1,1);if(r)e=r;
  r=checked_write(c,I9100_MC_BASE,1,7,7);if(r)e=r;
 }else{
  r=checked_write(c,I9100_MC_CODEC,15,1,1);if(r)e=r;
  r=checked_write(c,I9100_MC_BASE,1,6,6);if(r)e=r;
  r=checked_write(c,I9100_MC_BASE,3,0x1f,0x1f);if(r)e=r;
 }
 if(e)c->fault=1;else{c->cold_owned=0;c->cold_stage=0;c->fault=0;}
 return e;
}
int i9100_mc1n2_cold_stop(struct i9100_mc1n2 *c)
{
 int check=valid(c);if(check)return check;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=!c->enabled||!c->cold_owned?I9100_MC_DISABLED:cold_off_locked(c);
 c->io.release(c->io.cookie);return e;
}
static int clock_off_locked(struct i9100_mc1n2 *c)
{
 if(c->output_owner)return I9100_MC_BUSY;
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
 int check=valid(c);if(check)return check;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=!c->enabled||!c->clock_owned?I9100_MC_DISABLED:clock_off_locked(c);
 c->clock_cleanup_error=e;if(e&&!c->clock_error)c->clock_error=e;
 c->io.release(c->io.cookie);return e;
}
int i9100_mc1n2_clock_start(struct i9100_mc1n2 *c,const struct i9100_mc1n2_clock_guard *g)
{
 int check=valid(c);if(check)return check;
 if(!g||!g->lease_valid||!g->consumers_idle)return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=0;uint8_t identity=0,pad=0,start=0,fs=0,pll=0;
 if(!c->enabled||!c->cold_owned||c->cold_stage!=5U){e=I9100_MC_DISABLED;goto done;}
 if(c->clock_owned||c->fault){e=I9100_MC_BUSY;goto done;}
 if(c->mclk_hz!=24000000U||(c->sample_rate!=44100U&&c->sample_rate!=48000U))
  {e=I9100_MC_ARGUMENT;goto done;}
 if(g->lease_valid(g->cookie)){e=I9100_MC_NOT_READY;goto done;}
 if(g->consumers_idle(g->cookie)){e=I9100_MC_BUSY;goto done;}
 c->clock_error=c->clock_cleanup_error=0;c->clock_failure_stage=0;
 e=direct_read(c,8,&identity);if(!e&&(identity!=c->id||(identity!=0x78U&&identity!=0x79U)))e=I9100_MC_ID;
 if(!e)e=read_bank(c,I9100_MC_BASE,12,&pad);
 if(!e)e=read_bank(c,I9100_MC_MIXER,137,&start);
 if(!e)e=read_bank(c,I9100_MC_MIXER,135,&fs);
 if(!e)e=read_bank(c,c->id==0x78U?I9100_MC_BASE:I9100_MC_CODEC,c->id==0x78U?14U:15U,&pll);
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
 int check=valid(c);if(check)return check;
 if(verified!=1||mclk!=24000000U||(rate!=44100U&&rate!=48000U))return I9100_MC_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_MC_BUSY;
 int e=0;uint8_t identity=0;
 if(!c->enabled){e=I9100_MC_DISABLED;goto done;}
 if(c->fault||c->cold_owned){e=I9100_MC_BUSY;goto done;}
 e=direct_read(c,8,&identity);if(e)goto done;
 if(identity!=c->id||(identity!=0x78U&&identity!=0x79U)){e=I9100_MC_ID;goto done;}
 c->cold_owned=1;c->cold_stage=1;c->mclk_hz=mclk;c->sample_rate=rate;c->cold_error=0;c->failure_stage=0;
 /* Vendor AA/AB reset ordering. Both digital and analog reset defaults power
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
 if(identity==0x78U){
  /* AA PLL/CKSEL are BASE registers; its DP0 is explicitly powered first. */
  if(!e)e=checked_write(c,I9100_MC_BASE,14,1,1);
  if(!e)e=checked_write(c,I9100_MC_BASE,20,0,0x80);
  /* AA vendor init/power-up only program DIVR0/F0. R1/F1 have no AA
   * initialization sequence; their presence in the common config is not
   * permission to write them. */
  if(!e)e=checked_write(c,I9100_MC_BASE,15,28U,0xff);
  if(!e)e=checked_write(c,I9100_MC_BASE,16,86U,0xff);
  if(!e)e=checked_write(c,I9100_MC_BASE,1,6,7);
  if(!e)e=checked_write(c,I9100_MC_BASE,14,0,1);if(!e)e=pause(c,2000);
  if(!e)e=checked_write(c,I9100_MC_BASE,1,0,7);
 }else{
  if(!e)e=checked_write(c,I9100_MC_CODEC,15,1,1);
  if(!e)e=checked_write(c,I9100_MC_CODEC,4,0,0xc0);
  for(unsigned n=16;n<=19U&&!e;n++)e=checked_write(c,I9100_MC_CODEC,(uint8_t)n,(n&1U)?86U:28U,0xff);
  if(!e)e=checked_write(c,I9100_MC_CODEC,1,0x18,0xff);
  if(!e)e=checked_write(c,I9100_MC_CODEC,15,0,1);if(!e)e=pause(c,2000);
  if(!e)e=checked_write(c,I9100_MC_BASE,1,0,6);
 }
 if(!e)e=checked_write(c,I9100_MC_BASE,0,0,0x10);
 if(!e)e=checked_write(c,I9100_MC_BASE,3,0x1e,0x1f);
 if(!e)e=checked_write(c,I9100_MC_CODEC,79,0,0xff); /* DCL boost0dB, not vendor12dB */
 if(e)goto failed;
 c->cold_stage=3;
 if(identity==0x78U){
  /* AA vendor init explicitly unlocks ANA power via direct register60=D9
   * between the bank address and its initial power-down window write. */
  uint8_t bytes[6]={24U,2U,120U,0xd9U,26U,0x0fU};
  struct i9100_i2c_message message={0x3a,0,6,bytes};e=transfer(c,&message,1);
  uint8_t value=0;if(!e)e=read_bank(c,I9100_MC_ANALOG,2,&value);if(!e&&(value&15U)!=15U)e=I9100_MC_IO;
 }
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
