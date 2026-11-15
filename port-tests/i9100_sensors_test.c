/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_sensors.h"
#include "i9100_vibrator.h"
struct model {
 uint8_t regs[256],address,config;
 unsigned locked,transactions,fail_transaction,partial,delays;
 uint64_t ns;
 unsigned operations,fail_operation,sticky_failure,rail,pwm,timeout,cancel;
 uint32_t period,duty;
};
static struct model m;
static int claim(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void release(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint64_t now(void *c){return ((struct model *)c)->ns;}
static int delay(void *c,uint32_t us){struct model *p=c;assert(p->locked);p->ns+=us*1000ULL;p->delays++;return 0;}
static int transfer(void *c,const struct i9100_i2c_message *msgs,unsigned count,unsigned *done)
{
 struct model *p=c;assert(p->locked);*done=0;p->transactions++;
 if(p->transactions==p->fail_transaction)return -1;
 if(p->partial){*done=count-1;return 0;}
 assert(msgs[0].address==p->address&&!msgs[0].read);
 uint8_t reg=msgs[0].bytes[0];if(p->address!=0x0c)reg&=127U;
 if(count==1){assert(msgs[0].length==2);p->regs[reg]=msgs[0].bytes[1];}
 else {assert(count==2&&msgs[0].length==1&&msgs[1].read&&msgs[1].address==p->address);
  for(unsigned n=0;n<msgs[1].length;n++)msgs[1].bytes[n]=p->regs[(uint8_t)(reg+n)];
  if(p->address==0x0c&&reg==2&&msgs[1].length==8){p->regs[2]=0;p->regs[0xa]=0;}}
 *done=count;return 0;
}
static struct i9100_peripheral_i2c io={&m,transfer,claim,release,now,delay};
static struct i9100_sensor_config config(unsigned kind)
{
 struct i9100_sensor_config c;memset(&c,0,sizeof(c));c.kind=kind;
 c.axis[0][1]=-1;c.axis[1][0]=1;c.axis[2][2]=1;return c;
}
static void value(unsigned offset,int v){m.regs[offset]=(uint8_t)v;m.regs[offset+1]=(uint8_t)((unsigned)v>>8);}
static void sensor_st(unsigned kind)
{
 memset(&m,0,sizeof(m));m.address=kind==1?0x19:0x69;m.regs[0xf]=kind==1?0x33:0xd3;
 struct i9100_sensor s={0};struct i9100_sensor_config c=config(kind);
 struct i9100_sensor_sample sample,old;memset(&sample,0xa5,sizeof(sample));old=sample;
 assert(!i9100_sensor_init(&s,&io,&c));assert(!m.transactions);
 assert(i9100_sensor_read(&s,&sample)==I9100_SENSOR_DISABLED&&!m.transactions);
 assert(!i9100_sensor_enable(&s,1));assert(m.regs[0x23]==(kind==1?0x88:0x80));
 assert(i9100_sensor_read(&s,&sample)==I9100_SENSOR_NOT_READY&&!memcmp(&sample,&old,sizeof(old)));
 m.regs[0x27]=8;value(0x28,kind==1?1600:100);value(0x2a,kind==1?-3200:-200);value(0x2c,kind==1?32752:32767);
 assert(!i9100_sensor_read(&s,&sample));
 assert(sample.xyz[0]==(kind==1?200000:1750000)&&sample.xyz[1]==(kind==1?100000:875000));
 assert(sample.xyz[2]==(kind==1?2047000:286711250));assert(sample.sequence==1);
 old=sample;m.regs[0x27]=0x88;assert(i9100_sensor_read(&s,&sample)==I9100_SENSOR_OVERFLOW);
 assert(!memcmp(&sample,&old,sizeof(old))&&s.overflows==1);
 m.regs[0x27]=8;m.fail_transaction=m.transactions+2;
 assert(i9100_sensor_read(&s,&sample)==I9100_SENSOR_IO&&s.fault);
 assert(!memcmp(&sample,&old,sizeof(old)));m.fail_transaction=0;
 assert(!i9100_sensor_enable(&s,0)&&!m.regs[0x20]);
}
static void sensor_mag(void)
{
 memset(&m,0,sizeof(m));m.address=0x0c;m.regs[0]=0x48;m.regs[0x10]=128;m.regs[0x11]=192;m.regs[0x12]=64;
 struct i9100_sensor s={0};struct i9100_sensor_config c=config(3);
 assert(!i9100_sensor_init(&s,&io,&c));assert(!i9100_sensor_enable(&s,1));assert(m.delays==2&&!m.regs[0xa]);
 struct i9100_sensor_sample out,old;memset(&out,0xcd,sizeof(out));old=out;
 assert(i9100_sensor_read(&s,&out)==I9100_SENSOR_NOT_READY);
 assert(!i9100_sensor_begin(&s));assert(i9100_sensor_begin(&s)==I9100_SENSOR_BUSY);
 assert(i9100_sensor_read(&s,&out)==I9100_SENSOR_NOT_READY);
 m.regs[2]=1;value(3,100);value(5,-200);value(7,300);m.regs[9]=0;
 assert(!i9100_sensor_read(&s,&out));assert(out.xyz[0]==75000&&out.xyz[1]==30000&&out.xyz[2]==67500);
 assert(!m.regs[2]&&!s.converting);
 old=out;assert(!i9100_sensor_begin(&s));m.regs[2]=1;m.regs[9]=8;
 assert(i9100_sensor_read(&s,&out)==I9100_SENSOR_OVERFLOW&&!memcmp(&out,&old,sizeof(out))&&!s.converting);
 assert(!i9100_sensor_begin(&s));m.ns+=25000001ULL;
 assert(i9100_sensor_read(&s,&out)==I9100_SENSOR_TIMEOUT&&!s.converting&&!m.regs[0xa]);
 assert(!i9100_sensor_enable(&s,0));
 memset(&s,0,sizeof(s));m.regs[0]=0x49;assert(!i9100_sensor_init(&s,&io,&c));
 unsigned calls=m.transactions;assert(i9100_sensor_enable(&s,1)==I9100_SENSOR_ID&&m.transactions==calls+1);
 m.regs[0]=0x48;m.regs[0x10]=0;assert(i9100_sensor_enable(&s,1)==I9100_SENSOR_ID&&!s.active);
}
static void sensor_faults(void)
{
 for(unsigned point=1;point<=13;point++){
  memset(&m,0,sizeof(m));m.address=0x19;m.regs[0xf]=0x33;m.fail_transaction=point;
  struct i9100_sensor s={0};struct i9100_sensor_config c=config(1);assert(!i9100_sensor_init(&s,&io,&c));
  int error=i9100_sensor_enable(&s,1);
  if(error){assert(!s.active);if(s.fault){m.fail_transaction=0;assert(!i9100_sensor_enable(&s,0));}assert(!m.regs[0x20]);}
  else {m.fail_transaction=0;assert(!i9100_sensor_enable(&s,0));}
 }
 struct i9100_sensor s={0};struct i9100_sensor_config c=config(1);c.axis[0][0]=1;
 assert(i9100_sensor_init(&s,&io,&c)==I9100_SENSOR_ARGUMENT);
}
static int operation(struct model *p)
{p->operations++;return p->operations==p->fail_operation||p->sticky_failure?-1:0;}
static int hread(void *c,uint8_t reg,uint8_t *v){struct model *p=c;assert(p->locked&&reg==2);int e=operation(p);if(!e)*v=p->config;return e;}
static int hwrite(void *c,uint8_t reg,uint8_t v){struct model *p=c;assert(p->locked&&reg==2);p->config=v;return operation(p);}
static int rail(void *c,int enabled){struct model *p=c;assert(p->locked);p->rail=enabled;return operation(p);}
static int pwm_config(void *c,uint32_t period,uint32_t duty){struct model *p=c;assert(p->locked&&!p->pwm);p->period=period;p->duty=duty;return operation(p);}
static int pwm(void *c,int enabled){struct model *p=c;assert(p->locked);p->pwm=enabled;return operation(p);}
static int arm(void *c,uint32_t ms){struct model *p=c;assert(p->locked&&ms<=250);p->timeout=ms;return operation(p);}
static void cancel(void *c){struct model *p=c;assert(p->locked);p->timeout=0;p->cancel++;}
static struct i9100_vibrator_io vib_io={&m,claim,release,hread,hwrite,rail,pwm_config,pwm,arm,cancel,now};
static void vibration(void)
{
 memset(&m,0,sizeof(m));m.config=0x83;struct i9100_vibrator v={0};
 assert(!i9100_vibrator_init(&v,&vib_io,38022));assert(!m.operations);
 assert(i9100_vibrator_pulse(&v,20,50)==I9100_VIB_DISABLED&&!m.operations);
 assert(i9100_vibrator_stop(&v)==I9100_VIB_DISABLED&&!m.operations);
 assert(!i9100_vibrator_enable(&v,1));assert(!i9100_vibrator_pulse(&v,100,100));
 assert(m.pwm&&m.rail&&m.config==0xc3&&m.timeout==100&&v.active);
 assert(m.duty>=m.period/2&&m.duty<=(uint64_t)m.period*55/100);
 assert(i9100_vibrator_pulse(&v,20,50)==I9100_VIB_BUSY);
 m.ns=99999999;assert(!i9100_vibrator_timeout(&v)&&v.active);
 m.ns=100000000;assert(!i9100_vibrator_timeout(&v)&&!v.active&&!m.rail&&!m.pwm&&!(m.config&0x40));
 assert(i9100_vibrator_pulse(&v,251,50)==I9100_VIB_ARGUMENT);
 assert(i9100_vibrator_pulse(&v,20,101)==I9100_VIB_ARGUMENT);
 assert(!i9100_vibrator_enable(&v,0));
 for(unsigned point=1;point<=9;point++) {
  memset(&m,0,sizeof(m));m.config=0x83;memset(&v,0,sizeof(v));assert(!i9100_vibrator_init(&v,&vib_io,38022));
  assert(!i9100_vibrator_enable(&v,1));m.fail_operation=m.operations+point;
  int e=i9100_vibrator_pulse(&v,50,50);
  if(e){assert(!m.rail&&!m.pwm&&!(m.config&0x40));assert(!v.active||v.fault);}
  else {m.fail_operation=0;assert(!i9100_vibrator_stop(&v));}
 }
 memset(&m,0,sizeof(m));m.config=0x83;memset(&v,0,sizeof(v));assert(!i9100_vibrator_init(&v,&vib_io,38022));
 assert(!i9100_vibrator_enable(&v,1));assert(!i9100_vibrator_pulse(&v,30,50));m.sticky_failure=1;
 assert(i9100_vibrator_stop(&v)==I9100_VIB_IO&&v.fault);
 assert(i9100_vibrator_pulse(&v,30,50)==I9100_VIB_BUSY);m.sticky_failure=0;assert(!i9100_vibrator_stop(&v)&&!v.fault);
}
int main(void)
{sensor_st(1);sensor_st(2);sensor_mag();sensor_faults();vibration();puts("PASS sensor IDs/BDU/signed units/axis/ASA/ST2/data-ready/timeout/partial IO; haptic duty/time cap and applied-but-failed shutdown/quarantine");return 0;}
