/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sensors.h"
#include <string.h>
static int transfer(struct i9100_sensor *s,struct i9100_i2c_message *m,unsigned count)
{unsigned done=0;int e=s->io.transfer(s->io.cookie,m,count,&done);return e||done!=count?I9100_SENSOR_IO:0;}
static int read_reg(struct i9100_sensor *s,uint8_t reg,uint8_t *data,unsigned count)
{
 struct i9100_i2c_message m[2]={{s->address,0,1,&reg},{s->address,1,(uint16_t)count,data}};
 return transfer(s,m,2);
}
static int write_reg(struct i9100_sensor *s,uint8_t reg,uint8_t value)
{
 uint8_t bytes[2]={reg,value};struct i9100_i2c_message m={s->address,0,2,bytes};return transfer(s,&m,1);
}
static int signed16(const uint8_t *b)
{unsigned raw=b[0]|((unsigned)b[1]<<8);return raw&0x8000U?(int)raw-65536:(int)raw;}
static int axis_valid(const int8_t a[3][3])
{
 for(unsigned row=0;row<3;row++){
  unsigned sum=0;for(unsigned col=0;col<3;col++){if(a[row][col]<-1||a[row][col]>1)return 0;sum+=(unsigned)(a[row][col]*a[row][col]);}
  if(sum!=1)return 0;
 }
 for(unsigned col=0;col<3;col++){
  unsigned sum=0;for(unsigned row=0;row<3;row++)sum+=(unsigned)(a[row][col]*a[row][col]);if(sum!=1)return 0;
 }
 return 1;
}
int i9100_sensor_init(struct i9100_sensor *s,const struct i9100_peripheral_i2c *io,const struct i9100_sensor_config *c)
{
 if(!s||s->initialized||!io||!c||!io->transfer||!io->claim||!io->release||!io->now_ns||!io->delay_us||
    c->kind<I9100_SENSOR_K3DH||c->kind>I9100_SENSOR_AK8975||!axis_valid(c->axis))return I9100_SENSOR_ARGUMENT;
 for(unsigned n=0;n<3;n++)if(c->offset[n]<-1000000000||c->offset[n]>1000000000)return I9100_SENSOR_ARGUMENT;
 memset(s,0,sizeof(*s));s->io=*io;s->config=*c;s->initialized=1;
 s->address=c->kind==I9100_SENSOR_K3DH?0x19:(c->kind==I9100_SENSOR_K3G?0x69:0x0c);return 0;
}
static int st_verify(struct i9100_sensor *s,const uint8_t expected[5])
{
 uint8_t control[5];int e=read_reg(s,0xa0,control,sizeof(control));
 return e?e:(memcmp(control,expected,sizeof(control))?I9100_SENSOR_IO:0);
}
static int ak_stop(struct i9100_sensor *s)
{
 int e=write_reg(s,0x0a,0);if(e)return e;
 if(s->io.delay_us(s->io.cookie,100))return I9100_SENSOR_IO;
 uint8_t mode=255;e=read_reg(s,0x0a,&mode,1);
 return e?e:((mode&15U)?I9100_SENSOR_IO:0);
}
static int st_stop(struct i9100_sensor *s)
{
 int e=write_reg(s,0x20,0);
 for(unsigned n=1;n<5;n++){int restore=write_reg(s,(uint8_t)(0x20U+n),s->saved[n]);if(!e)e=restore;}
 uint8_t expected[5];memcpy(expected,s->saved,sizeof(expected));expected[0]=0;
 int check=st_verify(s,expected);if(!e)e=check;
 return e;
}
int i9100_sensor_enable(struct i9100_sensor *s,int enabled)
{
 if(!s||!s->initialized||(enabled!=0&&enabled!=1))return I9100_SENSOR_ARGUMENT;
 if(enabled&&(s->active||s->fault))return I9100_SENSOR_BUSY;
 if(!enabled&&!s->active&&!s->fault)return I9100_SENSOR_ARGUMENT;
 if(s->io.claim(s->io.cookie))return I9100_SENSOR_BUSY;
 int e=0;
 if((enabled&&(s->active||s->fault))||(!enabled&&!s->active&&!s->fault))
  {s->io.release(s->io.cookie);return I9100_SENSOR_BUSY;}
 if(!enabled){
  e=s->config.kind==I9100_SENSOR_AK8975?ak_stop(s):st_stop(s);
  if(!e){s->active=s->fault=s->converting=0;}else s->fault=1;
  goto done;
 }
 e=read_reg(s,s->config.kind==I9100_SENSOR_AK8975?0:0x0f,&s->id,1);if(e)goto done;
 uint8_t expected=s->config.kind==I9100_SENSOR_K3DH?0x33:(s->config.kind==I9100_SENSOR_K3G?0xd3:0x48);
 if(s->id!=expected){e=I9100_SENSOR_ID;goto done;}
 if(s->config.kind==I9100_SENSOR_AK8975){
  uint8_t mode=0;e=read_reg(s,0x0a,&mode,1);if(e)goto done;
  if(mode&15U){e=I9100_SENSOR_BUSY;goto done;}
  e=write_reg(s,0x0a,0x0f);
  if(!e&&s->io.delay_us(s->io.cookie,100))e=I9100_SENSOR_IO;
  if(!e)e=read_reg(s,0x10,s->asa,3);
  int stop=ak_stop(s);if(!e)e=stop;
  if(!e)for(unsigned n=0;n<3;n++)if(!s->asa[n]||s->asa[n]==255U)e=I9100_SENSOR_ID;
  if(stop)s->fault=1;
 }else {
  e=read_reg(s,0xa0,s->saved,5);if(e)goto done;
  if(s->saved[0]){e=I9100_SENSOR_BUSY;goto done;}
  /* Only a previously powered-down chip can be taken over. */
  for(unsigned n=1;n<5&&!e;n++)
   e=write_reg(s,(uint8_t)(0x20U+n),n==3U?(s->config.kind==I9100_SENSOR_K3DH?0x88:0x80):0);
  if(!e)e=write_reg(s,0x20,s->config.kind==I9100_SENSOR_K3DH?0x57:0x0f);
  if(!e){uint8_t expected[5]={s->config.kind==I9100_SENSOR_K3DH?0x57:0x0f,0,0,
                            s->config.kind==I9100_SENSOR_K3DH?0x88:0x80,0};
   e=st_verify(s,expected);}
  if(e){if(st_stop(s))s->fault=1;}
 }
 if(!e){s->active=1;s->converting=0;s->last_sample=0;}
done:
 s->io.release(s->io.cookie);return e;
}
int i9100_sensor_begin(struct i9100_sensor *s)
{
 if(!s||!s->initialized||s->config.kind!=I9100_SENSOR_AK8975)return I9100_SENSOR_ARGUMENT;
 if(!s->active)return I9100_SENSOR_DISABLED;if(s->fault)return I9100_SENSOR_IO;
 if(s->converting)return I9100_SENSOR_BUSY;
 if(s->io.claim(s->io.cookie))return I9100_SENSOR_BUSY;
 if(!s->active||s->fault||s->converting){s->io.release(s->io.cookie);return I9100_SENSOR_BUSY;}
 int e=write_reg(s,0x0a,1);if(!e){s->started=s->io.now_ns(s->io.cookie);s->converting=1;}
 else s->fault=1;s->io.release(s->io.cookie);return e;
}
int i9100_sensor_read(struct i9100_sensor *s,struct i9100_sensor_sample *out)
{
 if(!s||!s->initialized||!out)return I9100_SENSOR_ARGUMENT;
 if(!s->active)return I9100_SENSOR_DISABLED;if(s->fault)return I9100_SENSOR_IO;
 if(s->config.kind==I9100_SENSOR_AK8975&&!s->converting)return I9100_SENSOR_NOT_READY;
 if(s->io.claim(s->io.cookie))return I9100_SENSOR_BUSY;
 if(!s->active||s->fault){s->io.release(s->io.cookie);return I9100_SENSOR_DISABLED;}
 int e=0;uint8_t status=0,bytes[8]={0};int64_t xyz[3]={0};
 uint64_t n=s->io.now_ns(s->io.cookie);
 if(n<s->last_sample){e=I9100_SENSOR_TIMEOUT;goto done;}
 if(s->config.kind==I9100_SENSOR_AK8975){
  if(n<s->started||n-s->started>25000000ULL){
   e=I9100_SENSOR_TIMEOUT;if(ak_stop(s))s->fault=1;else s->converting=0;goto done;
  }
  e=read_reg(s,0x02,&status,1);if(e)goto done;
  if(!(status&1U)){e=I9100_SENSOR_NOT_READY;goto done;}
  e=read_reg(s,0x02,bytes,8);if(e)goto done;
  s->converting=0;
  if(!(bytes[0]&1U)){e=I9100_SENSOR_NOT_READY;goto done;}
  if((bytes[7]&12U)||(bytes[0]&2U)){e=I9100_SENSOR_OVERFLOW;s->overflows++;goto done;}
  for(unsigned axis=0;axis<3;axis++)xyz[axis]=(int64_t)signed16(&bytes[1U+2U*axis])*300*(s->asa[axis]+128U)/256;
 }else {
  e=read_reg(s,0x27,&status,1);if(e)goto done;
  if(!(status&8U)){e=I9100_SENSOR_NOT_READY;goto done;}
  e=read_reg(s,0xa8,bytes,6);if(e)goto done;
  if(status&128U){e=I9100_SENSOR_OVERFLOW;s->overflows++;goto done;}
  for(unsigned axis=0;axis<3;axis++){
   int value=signed16(&bytes[2U*axis]);
   if(s->config.kind==I9100_SENSOR_K3DH){if(value%16){e=I9100_SENSOR_IO;goto done;}xyz[axis]=(int64_t)(value/16)*1000;}
   else xyz[axis]=(int64_t)value*8750;
  }
 }
 struct i9100_sensor_sample sample;memset(&sample,0,sizeof(sample));
 sample.timestamp_ns=n;sample.kind=s->config.kind;sample.status=status;
 if(s->sequence==UINT32_MAX){e=I9100_SENSOR_OVERFLOW;goto done;}
 for(unsigned row=0;row<3;row++){
  int64_t value=-((int64_t)s->config.offset[row]);
  for(unsigned col=0;col<3;col++)value+=s->config.axis[row][col]*xyz[col];
  if(value<INT32_MIN||value>INT32_MAX){e=I9100_SENSOR_OVERFLOW;goto done;}
  sample.xyz[row]=(int32_t)value;
 }
 sample.sequence=++s->sequence;s->last_sample=n;*out=sample;
done:
 if(e==I9100_SENSOR_IO)s->fault=1;
 s->io.release(s->io.cookie);return e;
}
