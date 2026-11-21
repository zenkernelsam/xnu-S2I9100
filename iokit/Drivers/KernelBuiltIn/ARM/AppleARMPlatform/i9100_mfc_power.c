/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_power.h"
#include <string.h>
enum {GATE=0xc928,SOURCE=0xc228,DIV=0xc528,DIV_BUSY=0xc628,POWER=0x3c40,STATUS=0x3c44,
 LOCAL=7,SOURCE_MASK=0x101,DIV_MASK=15,IRQ_BIT=1U<<30,MPLL_MASK=0xa3ff3f07U};
static const uint8_t regions[MP_FIELD_COUNT]={MP_CHIP,MP_CMU,MP_CMU,MP_CMU,MP_CMU,MP_CMU,MP_CMU,MP_CMU,MP_PMU,MP_PMU,MP_GIC,MP_GIC};
static const uint32_t offsets[MP_FIELD_COUNT]={0,0x14200,0x14108,0x1410c,SOURCE,DIV,DIV_BUSY,GATE,POWER,STATUS,0x10c,0x30c};
unsigned i9100_mfc_power_region(unsigned f){return f<MP_FIELD_COUNT?regions[f]:UINT32_MAX;}
unsigned i9100_mfc_power_offset(unsigned f){return f<MP_FIELD_COUNT?offsets[f]:UINT32_MAX;}
static uint32_t rd(struct i9100_mfc_power *p,unsigned region,unsigned off){return p->io.read32(p->io.cookie,region,off);}
static int valid(struct i9100_mfc_power *p){return !p||!p->initialized?I9100_MP_ARGUMENT:p->self!=(uintptr_t)p?I9100_MP_OWNER:0;}
static int enter(struct i9100_mfc_power *p){int e=valid(p);return e?e:p->io.lock(p->io.cookie)?I9100_MP_BUSY:0;}
static int error(struct i9100_mfc_power *p,int e){if(!p->error)p->error=e;p->fault=1;return e;}
static int exact_field(struct i9100_mfc_power *p,unsigned region,unsigned off,uint32_t mask,uint32_t value)
{
 uint32_t old=rd(p,region,off);if(old==UINT32_MAX)return I9100_MP_IO;
 p->io.write32(p->io.cookie,region,off,(old&~mask)|(value&mask));p->io.barrier(p->io.cookie);
 uint32_t readback=rd(p,region,off);
 return readback!=UINT32_MAX&&(readback&mask)==(value&mask)?0:I9100_MP_IO;
}
static int wait_field(struct i9100_mfc_power *p,unsigned region,unsigned off,uint32_t mask,uint32_t value)
{
 uint64_t start=p->io.now_ns(p->io.cookie);
 for(unsigned n=0;n<=1000U;n++){
  uint64_t now=p->io.now_ns(p->io.cookie);if(now<start||now-start>1000000ULL)return I9100_MP_TIMEOUT;
  uint32_t read=rd(p,region,off);if(read==UINT32_MAX)return I9100_MP_IO;if((read&mask)==value)return 0;
  if(n==1000U||now-start>=1000000ULL)break;
  if(p->io.delay_us(p->io.cookie,1))return I9100_MP_IO;
 }return I9100_MP_TIMEOUT;
}
int i9100_mfc_power_parent(uint32_t xtal,uint32_t source,uint32_t con0,uint32_t max,
 uint32_t *parent,uint32_t *div,uint32_t *clock)
{
 if(!parent||!div||!clock||xtal!=24000000U||max<10000000U||max>200000000U||source==UINT32_MAX)return I9100_MP_ARGUMENT;
 uint64_t hz=xtal;
 if(source&(1U<<8)){
  uint32_t m=(con0>>16)&1023U,p=(con0>>8)&63U,s=con0&7U;
  if(con0==UINT32_MAX||(con0&0xa0000000U)!=0xa0000000U||!m||!p||!s)return I9100_MP_CLOCK;
  hz=(uint64_t)xtal*m/((uint64_t)p<<(s-1U));
 }
 if(hz<10000000ULL||hz>1600000000ULL)return I9100_MP_CLOCK;
 uint64_t divisor=(hz+max-1U)/max;
 if(!divisor||divisor>16U||hz/divisor<10000000U)return I9100_MP_CLOCK;
 *parent=(uint32_t)hz;*div=(uint32_t)divisor;*clock=(uint32_t)(hz/divisor);return 0;
}
static int snapshot(struct i9100_mfc_power *p,uint32_t out[MP_FIELD_COUNT])
{
 for(unsigned f=0;f<MP_FIELD_COUNT;f++)out[f]=rd(p,regions[f],offsets[f]);
 for(unsigned f=0;f<MP_FIELD_COUNT;f++)if(out[f]==UINT32_MAX)return I9100_MP_IO;
 return 0;
}
static int roots(struct i9100_mfc_power *p)
{
 uint32_t source=rd(p,MP_CMU,0x14200),pll=rd(p,MP_CMU,0x14108);
 p->io.barrier(p->io.cookie);
 uint32_t second=rd(p,MP_CMU,0x14200),other=rd(p,MP_CMU,0x14108),parent,div,clock;
 if(source==UINT32_MAX||second==UINT32_MAX||((source^second)&(1U<<8)))return I9100_MP_CLOCK;
 if((source&(1U<<8))&&((pll^other)&MPLL_MASK))return I9100_MP_CLOCK;
 int e=i9100_mfc_power_parent(p->config.xxti_hz,second,other,p->config.maximum_clock_hz,&parent,&div,&clock);
 if(e)return e;
 if(((second^p->cpu_source)&(1U<<8))||((second&(1U<<8))&&((other^p->mpll0)&MPLL_MASK))||
    parent!=p->parent_hz||div!=p->divider||clock!=p->clock_hz)return I9100_MP_CLOCK;
 return 0;
}
static int powered(struct i9100_mfc_power *p)
{
 uint32_t gate=rd(p,MP_CMU,GATE),config=rd(p,MP_PMU,POWER),status=rd(p,MP_PMU,STATUS);
 if(gate==UINT32_MAX||config==UINT32_MAX||status==UINT32_MAX)return I9100_MP_IO;
 return (gate&LOCAL)==LOCAL&&(config&LOCAL)==LOCAL&&(status&LOCAL)==LOCAL?0:I9100_MP_OWNER;
}
static int engine_idle(struct i9100_mfc_power *p)
{
 int e=powered(p);if(e)return e;
 p->confirm_reset=rd(p,MP_MFC,0);p->confirm_mc=rd(p,MP_MFC,0x510);
 if(p->confirm_reset==UINT32_MAX||p->confirm_mc==UINT32_MAX)return I9100_MP_IO;
 if((p->confirm_reset&1U)||(p->confirm_mc&3U)||rd(p,MP_MFC,0x30)||rd(p,MP_MFC,0x44)||rd(p,MP_MFC,8))return I9100_MP_BUSY;
 for(unsigned unit=MP_MMU_L;unit<=MP_MMU_R;unit++)if(rd(p,unit,0)||rd(p,unit,0x18))return I9100_MP_BUSY;
 return 0;
}
static int divider_set(struct i9100_mfc_power *p,uint32_t value)
{
 uint32_t current=rd(p,MP_CMU,DIV);if(current==UINT32_MAX)return I9100_MP_IO;
 p->previous_divider=current&DIV_MASK;p->divider_written=1;p->working_divider=value;
 int e=exact_field(p,MP_CMU,DIV,DIV_MASK,value);
 return e?e:wait_field(p,MP_CMU,DIV_BUSY,UINT32_MAX,0);
}
int i9100_mfc_power_init(struct i9100_mfc_power *p,const struct i9100_mfc_power_io *io,const struct i9100_mfc_power_config *cfg)
{
 if(!p||p->initialized||!io||!cfg||cfg->board_data_reviewed!=1U||cfg->xxti_hz!=24000000U||
    cfg->maximum_clock_hz<10000000U||cfg->maximum_clock_hz>200000000U||!io->read32||!io->write32||!io->lock||!io->unlock||
    !io->now_ns||!io->delay_us||!io->barrier||!io->exclusive||!io->consumers_idle)return I9100_MP_ARGUMENT;
 memset(p,0,sizeof(*p));p->self=(uintptr_t)p;p->io=*io;p->config=*cfg;p->initialized=1;return 0;
}
int i9100_mfc_power_enable(struct i9100_mfc_power *p,int on)
{
 if(on!=0&&on!=1)return I9100_MP_ARGUMENT;int e=enter(p);if(e)return e;
 if(p->owned)e=I9100_MP_BUSY;else p->enabled=(unsigned)on;p->io.unlock(p->io.cookie);return e;
}
int i9100_mfc_power_acquire(struct i9100_mfc_power *p)
{
 int e=enter(p);if(e)return e;
 if(!p->enabled){e=I9100_MP_DISABLED;goto out;}
 if(p->owned||p->fault||p->generation==UINT32_MAX){e=I9100_MP_BUSY;goto out;}
 if(p->io.exclusive(p->io.cookie)){e=I9100_MP_OWNER;goto out;}
 if(p->io.consumers_idle(p->io.cookie)){e=I9100_MP_BUSY;goto out;}
 e=snapshot(p,p->before);if(e)goto out;p->io.barrier(p->io.cookie);e=snapshot(p,p->after);if(e)goto out;
 for(unsigned f=0;f<MP_FIELD_COUNT;f++){
  uint32_t mask=f==MP_CPU_SOURCE?1U<<8:f==MP_MPLL0?MPLL_MASK:f==MP_MPLL1?0:
    f==MP_SOURCE?SOURCE_MASK:f==MP_DIVIDER?DIV_MASK:f==MP_GATE||f==MP_CONFIG||f==MP_STATUS?LOCAL:
    f==MP_IRQ_ENABLE||f==MP_IRQ_ACTIVE?IRQ_BIT:UINT32_MAX;
  if((p->before[f]^p->after[f])&mask){e=I9100_MP_OWNER;goto out;}
 }
 if((p->after[MP_CHIP_ID]&0xfffe0000U)!=(0x43210000U&0xfffe0000U)){e=I9100_MP_OWNER;goto out;}
 p->saved_power=p->after[MP_CONFIG]&LOCAL;p->saved_gate=p->after[MP_GATE]&LOCAL;
 if((p->saved_power!=0U&&p->saved_power!=LOCAL)||(p->after[MP_STATUS]&LOCAL)!=p->saved_power||
    p->after[MP_DIV_BUSY]||p->after[MP_IRQ_ENABLE]&IRQ_BIT||p->after[MP_IRQ_ACTIVE]&IRQ_BIT){e=I9100_MP_BUSY;goto out;}
 e=i9100_mfc_power_parent(p->config.xxti_hz,p->after[MP_CPU_SOURCE],p->after[MP_MPLL0],p->config.maximum_clock_hz,
                         &p->parent_hz,&p->divider,&p->clock_hz);if(e)goto out;
 p->cpu_source=p->after[MP_CPU_SOURCE];p->mpll0=p->after[MP_MPLL0];
 p->saved_source=p->after[MP_SOURCE]&SOURCE_MASK;p->saved_divider=p->after[MP_DIVIDER]&DIV_MASK;
 p->owned=1;p->ready=p->closing=0;p->generation++;p->error=p->cleanup_error=0;
 if(p->saved_gate!=LOCAL){p->gate_written=1;e=exact_field(p,MP_CMU,GATE,LOCAL,LOCAL);if(e)goto fail;}
 if(p->saved_power!=LOCAL){p->power_written=1;e=exact_field(p,MP_PMU,POWER,LOCAL,LOCAL);if(e)goto fail;}
 e=wait_field(p,MP_PMU,STATUS,LOCAL,LOCAL);if(e)goto fail;
 /* The first actual MFC read is also the required powered-bus completion
  * confirmation; a foreign live engine is never reset by its supplier. */
 e=engine_idle(p);if(e)goto fail;e=roots(p);if(e)goto fail;
 /* Slow the leaf before changing its parent; only lower the divisor after
  * the exact mux write completes. Do not transiently multiply a stopped
  * engine's clock by switching a fast parent with an inherited /1 divider. */
 if(p->saved_source){
  if(p->saved_divider!=15U){e=divider_set(p,15U);if(e)goto fail;}
  p->source_written=1;e=exact_field(p,MP_CMU,SOURCE,SOURCE_MASK,0);if(e)goto fail;
 }
 if((rd(p,MP_CMU,DIV)&DIV_MASK)!=p->divider-1U){e=divider_set(p,p->divider-1U);if(e)goto fail;}
 e=wait_field(p,MP_CMU,DIV_BUSY,UINT32_MAX,0);if(e)goto fail;e=roots(p);if(e)goto fail;
 if(p->io.exclusive(p->io.cookie)||(rd(p,MP_CMU,SOURCE)&SOURCE_MASK)||
    (rd(p,MP_CMU,DIV)&DIV_MASK)!=p->divider-1U){e=I9100_MP_OWNER;goto fail;}
 p->ready=1;goto out;
fail:e=error(p,e);
out:p->io.unlock(p->io.cookie);return e;
}
int i9100_mfc_power_validate(struct i9100_mfc_power *p)
{
 int e=enter(p);if(e)return e;
 if(!p->owned||!p->ready||p->closing)e=I9100_MP_OWNER;
 else if(p->io.exclusive(p->io.cookie))e=I9100_MP_OWNER;
 else if((e=powered(p))==0&&(e=roots(p))==0){
  if(rd(p,MP_CMU,SOURCE)==UINT32_MAX||rd(p,MP_CMU,DIV)==UINT32_MAX||
     rd(p,MP_CMU,SOURCE)&SOURCE_MASK||(rd(p,MP_CMU,DIV)&DIV_MASK)!=p->divider-1U||rd(p,MP_CMU,DIV_BUSY)||
     rd(p,MP_GIC,0x10c)&IRQ_BIT||rd(p,MP_GIC,0x30c)&IRQ_BIT)e=I9100_MP_OWNER;
  for(unsigned unit=MP_MMU_L;unit<=MP_MMU_R&&!e;unit++)if(rd(p,unit,0)||rd(p,unit,0x18))e=I9100_MP_OWNER;
 }
 if(e&&p->owned)error(p,e);p->io.unlock(p->io.cookie);return e;
}
static int restore(struct i9100_mfc_power *p,unsigned region,unsigned off,uint32_t mask,uint32_t saved,uint32_t own)
{
 uint32_t v=rd(p,region,off);if(v==UINT32_MAX)return I9100_MP_IO;v&=mask;
 if(v!=saved&&v!=own)return I9100_MP_OWNER;return v==saved?0:exact_field(p,region,off,mask,saved);
}
int i9100_mfc_power_release(struct i9100_mfc_power *p)
{
 int e=enter(p);if(e)return e;
 if(!p->owned){e=I9100_MP_OWNER;goto out;}
 if(p->io.exclusive(p->io.cookie)){e=I9100_MP_OWNER;goto out;}
 if(p->io.consumers_idle(p->io.cookie)){e=I9100_MP_BUSY;goto out;}
 p->closing=1;
 uint32_t config=rd(p,MP_PMU,POWER),status=rd(p,MP_PMU,STATUS);
 if(config==UINT32_MAX||status==UINT32_MAX){e=I9100_MP_IO;goto fail;}
 uint32_t gate=rd(p,MP_CMU,GATE);if(gate==UINT32_MAX){e=I9100_MP_IO;goto fail;}
 if((gate&LOCAL)==p->saved_gate&&p->saved_gate!=LOCAL&&!p->power_written&&!p->source_written&&!p->divider_written&&
    (config&LOCAL)==p->saved_power&&(status&LOCAL)==p->saved_power){
  /* Dropped acquisition left an inherited gated state unchanged. Nothing
   * was powered or re-clocked by us; release its software claim without
   * attempting an unsafe read of a gated warm engine. */
 }
 else if((config&LOCAL)==LOCAL&&(status&LOCAL)==LOCAL){e=engine_idle(p);if(e)goto fail;}
 else if((config&LOCAL)==p->saved_power&&(status&LOCAL)==p->saved_power&&!p->power_written&&
         !p->source_written&&!p->divider_written){
  /* A failed gate acquisition never changed the power/domain/engine. Its
   * attempted gate write may be undone without reading a gated device. */
 }
 else if((config&LOCAL)||(status&LOCAL)||p->source_written||p->divider_written){e=I9100_MP_OWNER;goto fail;}
 if(p->source_written){
  uint32_t v=rd(p,MP_CMU,DIV);if(v==UINT32_MAX){e=I9100_MP_IO;goto fail;}
  if((v&DIV_MASK)!=p->working_divider&&(v&DIV_MASK)!=p->previous_divider&&(v&DIV_MASK)!=p->saved_divider){e=I9100_MP_OWNER;goto fail;}
  if((v&DIV_MASK)!=15U){e=divider_set(p,15U);if(e)goto fail;}
  e=restore(p,MP_CMU,SOURCE,SOURCE_MASK,p->saved_source,0);if(e)goto fail;p->source_written=0;
 }
 if(p->divider_written){
  uint32_t v=rd(p,MP_CMU,DIV);if(v==UINT32_MAX){e=I9100_MP_IO;goto fail;}
  if((v&DIV_MASK)!=p->working_divider&&(v&DIV_MASK)!=p->previous_divider&&(v&DIV_MASK)!=p->saved_divider){e=I9100_MP_OWNER;goto fail;}
  e=restore(p,MP_CMU,DIV,DIV_MASK,p->saved_divider,v&DIV_MASK);if(e)goto fail;
  e=wait_field(p,MP_CMU,DIV_BUSY,UINT32_MAX,0);if(e)goto fail;p->divider_written=0;}
 if(p->power_written){e=restore(p,MP_PMU,POWER,LOCAL,p->saved_power,LOCAL);if(e)goto fail;
  e=wait_field(p,MP_PMU,STATUS,LOCAL,p->saved_power);if(e)goto fail;p->power_written=0;}
 else if((rd(p,MP_PMU,POWER)&LOCAL)!=p->saved_power||(rd(p,MP_PMU,STATUS)&LOCAL)!=p->saved_power){e=I9100_MP_OWNER;goto fail;}
 if(p->gate_written){e=restore(p,MP_CMU,GATE,LOCAL,p->saved_gate,LOCAL);if(e)goto fail;p->gate_written=0;}
 else if((rd(p,MP_CMU,GATE)&LOCAL)!=p->saved_gate){e=I9100_MP_OWNER;goto fail;}
 p->owned=p->ready=p->closing=p->fault=0;p->cleanup_error=0;goto out;
fail:p->cleanup_error=e;error(p,e);
out:p->io.unlock(p->io.cookie);return e;
}
