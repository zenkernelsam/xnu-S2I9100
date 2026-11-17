/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_vibrator.h"
#include <string.h>
enum {CONFIG2=2,MOTOR_ENABLE=0x40,LRA_EXTERNAL_DIV256=0x83};
static int shutdown(struct i9100_vibrator *v)
{
 int e=0;v->io.cancel_timeout(v->io.cookie);
 if(v->io.pwm_enable(v->io.cookie,0))e=I9100_VIB_IO;
 if(v->io.haptic_write(v->io.cookie,CONFIG2,v->saved_config&~MOTOR_ENABLE))e=I9100_VIB_IO;
 uint8_t verify=0xff;
 if(v->io.haptic_read(v->io.cookie,CONFIG2,&verify)||(verify&MOTOR_ENABLE))e=I9100_VIB_IO;
 if(v->rail_owned){if(v->io.rail(v->io.cookie,0))e=I9100_VIB_IO;else v->rail_owned=0;}
 if(e)v->fault=1;else{v->active=v->fault=0;v->deadline=0;}
 return e;
}
int i9100_vibrator_init(struct i9100_vibrator *v,const struct i9100_vibrator_io *io,uint32_t period)
{
 if(!v||v->initialized||!io||!io->lock||!io->unlock||!io->haptic_read||!io->haptic_write||!io->rail||
    !io->pwm_config||!io->pwm_enable||!io->arm_timeout||!io->cancel_timeout||!io->now_ns||
    period<30000U||period>50000U)return I9100_VIB_ARGUMENT;
 memset(v,0,sizeof(*v));v->io=*io;v->period_ns=period;v->initialized=1;return 0;
}
int i9100_vibrator_enable(struct i9100_vibrator *v,int enabled)
{
 if(!v||!v->initialized||(enabled!=0&&enabled!=1))return I9100_VIB_ARGUMENT;
 if(v->io.lock(v->io.cookie))return I9100_VIB_BUSY;
 int e=0;
 if(enabled){
  if(v->enabled||v->active||v->fault)e=I9100_VIB_BUSY;
  else if(v->io.haptic_read(v->io.cookie,CONFIG2,&v->saved_config))e=I9100_VIB_IO;
  else if(v->saved_config&MOTOR_ENABLE)e=I9100_VIB_BUSY;
  else if(v->io.pwm_enable(v->io.cookie,0))e=I9100_VIB_IO;
  else v->enabled=1;
 }else if(!v->enabled&&!v->fault&&!v->rail_owned)e=I9100_VIB_DISABLED;
 else{e=shutdown(v);if(!e)v->enabled=0;}
 v->io.unlock(v->io.cookie);return e;
}
int i9100_vibrator_pulse(struct i9100_vibrator *v,uint32_t ms,unsigned amplitude)
{
 if(!v||!v->initialized||!ms||ms>250U||!amplitude||amplitude>100U)return I9100_VIB_ARGUMENT;
 if(v->io.lock(v->io.cookie))return I9100_VIB_BUSY;
 int e=0;
 if(!v->enabled){e=I9100_VIB_DISABLED;goto done;}
 if(v->active||v->fault||v->rail_owned){e=I9100_VIB_BUSY;goto done;}
 /* amplitude100/1000 maps to +5% duty; do not treat 0% duty as weak LRA. */
 v->duty_ns=(uint32_t)((uint64_t)v->period_ns*(1000U+amplitude)/2000U);
 v->started=v->io.now_ns(v->io.cookie);
 if(v->started>UINT64_MAX-(uint64_t)ms*1000000ULL){e=I9100_VIB_TIMEOUT;goto done;}
 if(v->io.pwm_enable(v->io.cookie,0)||v->io.pwm_config(v->io.cookie,v->period_ns,v->duty_ns))
  {e=I9100_VIB_IO;goto unwind;}
 v->rail_owned=1; /* A failed power write may nevertheless have reached the PMIC. */
 if(v->io.rail(v->io.cookie,1)){e=I9100_VIB_IO;goto unwind;}
 v->active=1;v->deadline=v->started+(uint64_t)ms*1000000ULL;
 if(v->io.haptic_write(v->io.cookie,CONFIG2,LRA_EXTERNAL_DIV256|MOTOR_ENABLE))
  {e=I9100_VIB_IO;goto unwind;}
 uint8_t verify=0;
 if(v->io.haptic_read(v->io.cookie,CONFIG2,&verify)||verify!=(LRA_EXTERNAL_DIV256|MOTOR_ENABLE))
  {e=I9100_VIB_IO;goto unwind;}
 /* Preparation belongs to the original budget. Arm only the time left;
  * a slow PMIC transaction must not extend the requested pulse. */
 {uint64_t now=v->io.now_ns(v->io.cookie);
  if(now<v->started||now>=v->deadline){e=I9100_VIB_TIMEOUT;goto unwind;}
  uint32_t remaining=(uint32_t)((v->deadline-now+999999ULL)/1000000ULL);
  if(v->io.arm_timeout(v->io.cookie,remaining)||v->io.pwm_enable(v->io.cookie,1))
   {e=I9100_VIB_IO;goto unwind;}
  now=v->io.now_ns(v->io.cookie);
  if(now<v->started||now>=v->deadline){e=I9100_VIB_TIMEOUT;goto unwind;}}
 goto done;
unwind:
 (void)shutdown(v);
done:
 v->io.unlock(v->io.cookie);return e;
}
int i9100_vibrator_stop(struct i9100_vibrator *v)
{
 if(!v||!v->initialized)return I9100_VIB_ARGUMENT;
 if(v->io.lock(v->io.cookie))return I9100_VIB_BUSY;
 if(!v->enabled&&!v->fault&&!v->rail_owned){v->io.unlock(v->io.cookie);return I9100_VIB_DISABLED;}
 int e=shutdown(v);v->io.unlock(v->io.cookie);return e;
}
int i9100_vibrator_timeout(struct i9100_vibrator *v)
{
 if(!v||!v->initialized)return I9100_VIB_ARGUMENT;
 if(v->io.lock(v->io.cookie))return I9100_VIB_BUSY;
 int e=0;uint64_t now=v->io.now_ns(v->io.cookie);
 if(v->active&&(now>=v->deadline||now<v->started)){e=shutdown(v);if(!e&&now<v->started)e=I9100_VIB_TIMEOUT;}
 else if(v->active){
  uint32_t remaining=(uint32_t)((v->deadline-now+999999ULL)/1000000ULL);
  if(v->io.arm_timeout(v->io.cookie,remaining)){(void)shutdown(v);e=I9100_VIB_IO;}
 }
 v->io.unlock(v->io.cookie);return e;
}
