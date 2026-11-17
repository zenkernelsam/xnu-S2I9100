/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_pwm_haptic.h"
#include <string.h>
enum {PRESCALE=0,MUX=4,CONTROL=8,COUNT=0x18,COMPARE=0x1c,INTERRUPT=0x44,
 START=1U<<8,UPDATE=1U<<9,INVERT=1U<<10,RELOAD=1U<<11,MASK=0xf00U};
static uint32_t rd(struct i9100_pwm_haptic *p,unsigned off){return p->io.read32(p->io.cookie,off);}
static void wr(struct i9100_pwm_haptic *p,unsigned off,uint32_t v){p->io.write32(p->io.cookie,off,v);}
int i9100_pwm_haptic_init(struct i9100_pwm_haptic *p,const struct i9100_pwm_io *io,uint32_t clock)
{
 if(!p||p->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||clock<1000000U||clock>200000000U)return I9100_PWM_ARGUMENT;
 memset(p,0,sizeof(*p));p->io=*io;p->clock_hz=clock;p->initialized=1;return 0;
}
int i9100_pwm_haptic_enable(struct i9100_pwm_haptic *p,int enabled)
{
 if(!p||!p->initialized||(enabled!=0&&enabled!=1))return I9100_PWM_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PWM_BUSY;
 int e=p->prepared||p->running||p->fault?I9100_PWM_BUSY:0;if(!e)p->enabled=enabled;p->io.unlock(p->io.cookie);return e;
}
int i9100_pwm_haptic_configure(struct i9100_pwm_haptic *p,uint32_t period,uint32_t duty,int verified)
{
 if(!p||!p->initialized||!verified||period<30000U||period>50000U||
    (uint64_t)duty*2U<period||(uint64_t)duty*100U>(uint64_t)period*55U)return I9100_PWM_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PWM_BUSY;
 int e=0;if(!p->enabled){e=I9100_PWM_DISABLED;goto done;}if(p->prepared||p->running||p->fault){e=I9100_PWM_BUSY;goto done;}
 if((rd(p,CONTROL)&(START|UPDATE))||(rd(p,INTERRUPT)&2U)){e=I9100_PWM_BUSY;goto done;}
 uint32_t prescale=(rd(p,PRESCALE)&255U)+1U;uint64_t divisor=(uint64_t)prescale*1000000000ULL;
 uint32_t ticks=(uint32_t)(((uint64_t)p->clock_hz*period+divisor/2U)/divisor);
 uint32_t high=(uint32_t)(((uint64_t)p->clock_hz*duty)/divisor);
 if(ticks<20U||!high||high>=ticks){e=I9100_PWM_CLOCK;goto done;}
 /* Round towards zero-drive when quantization would exceed the safe limit. */
 if((uint64_t)high*100U>(uint64_t)ticks*55U)high=ticks*55U/100U;
 if((uint64_t)high*2U<ticks)high=(ticks+1U)/2U;
 p->prescaler=prescale;p->saved_control=rd(p,CONTROL)&MASK;p->saved_mux=rd(p,MUX)&0xf0U;
 p->saved_count=rd(p,COUNT);p->saved_compare=rd(p,COMPARE);p->count=ticks-1U;p->compare=ticks-high-1U;
 p->actual_period_ns=(uint32_t)((uint64_t)ticks*divisor/p->clock_hz);
 p->actual_duty_ns=(uint32_t)((uint64_t)high*divisor/p->clock_hz);p->prepared=1;
 wr(p,MUX,rd(p,MUX)&~0xf0U);wr(p,COUNT,p->count);wr(p,COMPARE,p->compare);
 uint32_t control=(rd(p,CONTROL)&~MASK)|INVERT|RELOAD;
 wr(p,CONTROL,control|UPDATE);wr(p,CONTROL,control);
 if(rd(p,COUNT)!=p->count||rd(p,COMPARE)!=p->compare||(rd(p,MUX)&0xf0U)||(rd(p,CONTROL)&MASK)!=(INVERT|RELOAD))
  {p->fault=1;e=I9100_PWM_IO;}
done:
 p->io.unlock(p->io.cookie);return e;
}
int i9100_pwm_haptic_run(struct i9100_pwm_haptic *p,int enabled)
{
 if(!p||!p->initialized||(enabled!=0&&enabled!=1))return I9100_PWM_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PWM_BUSY;
 int e=0;if(!p->enabled||!p->prepared){e=I9100_PWM_DISABLED;goto done;}
 if(enabled&&(p->fault||(rd(p,PRESCALE)&255U)+1U!=p->prescaler)){
  wr(p,CONTROL,rd(p,CONTROL)&~START);p->fault=1;
  if(rd(p,CONTROL)&START)e=I9100_PWM_IO;else{p->running=0;e=I9100_PWM_CLOCK;}goto done;
 }
 if(enabled&&(rd(p,COUNT)!=p->count||rd(p,COMPARE)!=p->compare||(rd(p,MUX)&0xf0U)||
              (rd(p,CONTROL)&MASK)!=(INVERT|RELOAD|(p->running?START:0U))||(rd(p,INTERRUPT)&2U))){
  wr(p,CONTROL,rd(p,CONTROL)&~START);p->fault=1;
  if(!(rd(p,CONTROL)&START))p->running=0;e=I9100_PWM_IO;goto done;
 }
 wr(p,CONTROL,(rd(p,CONTROL)&~START)|(enabled?START:0U));
 if(!!(rd(p,CONTROL)&START)!=(unsigned)enabled){p->fault=1;e=I9100_PWM_IO;}else p->running=enabled;
done:
 p->io.unlock(p->io.cookie);return e;
}
int i9100_pwm_haptic_release(struct i9100_pwm_haptic *p)
{
 if(!p||!p->initialized)return I9100_PWM_ARGUMENT;
 if(p->io.lock(p->io.cookie))return I9100_PWM_BUSY;
 int e=0;if(!p->prepared){e=I9100_PWM_DISABLED;goto done;}
 wr(p,CONTROL,rd(p,CONTROL)&~START);
 if(rd(p,CONTROL)&START){p->fault=1;e=I9100_PWM_IO;goto done;}
 wr(p,COUNT,p->saved_count);wr(p,COMPARE,p->saved_compare);
 wr(p,MUX,(rd(p,MUX)&~0xf0U)|p->saved_mux);wr(p,CONTROL,(rd(p,CONTROL)&~MASK)|p->saved_control);
 if(rd(p,COUNT)!=p->saved_count||rd(p,COMPARE)!=p->saved_compare||(rd(p,MUX)&0xf0U)!=p->saved_mux||(rd(p,CONTROL)&MASK)!=p->saved_control)
  {p->fault=1;e=I9100_PWM_IO;}else p->prepared=p->running=p->fault=0;
done:
 p->io.unlock(p->io.cookie);return e;
}
