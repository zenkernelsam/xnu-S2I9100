/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_camera_protocol.h"
#include <string.h>
static int transfer(struct i9100_camera *c,struct i9100_i2c_message *m,unsigned count)
{unsigned completed=0;int e=c->io.transfer(c->io.cookie,m,count,&completed);return e||completed!=count?I9100_CAM_IO:0;}
static int m5read(struct i9100_camera *c,uint8_t category,uint8_t reg,unsigned width,uint32_t *value)
{
 uint8_t command[5]={5,1,category,reg,(uint8_t)width},reply[5]={0};
 struct i9100_i2c_message m={0x1f,0,5,command};int e=transfer(c,&m,1);if(e)return e;
 m.read=1;m.length=(uint16_t)(width+1U);m.bytes=reply;e=transfer(c,&m,1);if(e)return e;
 if(reply[0]!=width+1U)return I9100_CAM_FRAME;
 uint32_t result=0;for(unsigned n=0;n<width;n++)result=(result<<8)|reply[n+1U];*value=result;return 0;
}
static int reg16write(struct i9100_camera *c,uint16_t reg,uint16_t value)
{
 uint8_t bytes[4]={(uint8_t)(reg>>8),(uint8_t)reg,(uint8_t)(value>>8),(uint8_t)value};
 struct i9100_i2c_message m={0x2d,0,4,bytes};return transfer(c,&m,1);
}
static int s5read(struct i9100_camera *c,uint16_t page,uint16_t address,uint16_t *value)
{
 int e=reg16write(c,0x002c,page);if(e)return e;e=reg16write(c,0x002e,address);if(e)return e;
 uint8_t reg[2]={0x0f,0x12},data[2]={0};struct i9100_i2c_message m[2]={{0x2d,0,2,reg},{0x2d,1,2,data}};
 e=transfer(c,m,2);if(!e)*value=(uint16_t)(((uint16_t)data[0]<<8)|data[1]);return e;
}
int i9100_camera_init(struct i9100_camera *c,const struct i9100_peripheral_i2c *io,enum i9100_camera_kind kind)
{
 if(!c||c->initialized||!io||!io->claim||!io->release||!io->transfer||!io->now_ns||!io->delay_us||kind<1||kind>2)
  return I9100_CAM_ARGUMENT;
 memset(c,0,sizeof(*c));c->io=*io;c->kind=kind;c->initialized=1;return 0;
}
int i9100_camera_enable(struct i9100_camera *c,int enabled,int verified)
{
 if(!c||!c->initialized||(enabled!=0&&enabled!=1))return I9100_CAM_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_CAM_BUSY;
 int e=0;
 if(!enabled){c->enabled=0;goto done;} /* power/clock lease release remains provider's job */
 if(!verified){e=I9100_CAM_ARGUMENT;goto done;}if(c->enabled||c->fault){e=I9100_CAM_BUSY;goto done;}
 if(c->kind==I9100_CAMERA_M5MO){
  e=m5read(c,0,1,1,&c->project);if(!e)e=m5read(c,0,2,2,&c->firmware);if(!e)e=m5read(c,0,4,2,&c->hardware);
  if(!e&&(!c->project||c->project==255U||!c->firmware||c->firmware==65535U||!c->hardware||c->hardware==65535U))e=I9100_CAM_ID;
 }else {
  /* The board's original init audits silicon/revision at D000:1006/1008. */
  uint16_t id=0,revision=0;e=s5read(c,0xd000,0x1006,&id);if(!e)e=s5read(c,0xd000,0x1008,&revision);
  c->hardware=id;c->firmware=revision;
  if(!e&&(id!=0x05baU||(revision&255U)!=0xa0U))e=I9100_CAM_ID;
 }
 if(!e)c->enabled=1;else if(e==I9100_CAM_IO)c->fault=1;
done:
 c->io.release(c->io.cookie);return e;
}
int i9100_m5mo_read(struct i9100_camera *c,uint8_t category,uint8_t reg,unsigned width,uint32_t *value)
{
 if(!c||!c->initialized||c->kind!=I9100_CAMERA_M5MO||!value||category>=15U||(width!=1U&&width!=2U&&width!=4U))return I9100_CAM_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_CAM_BUSY;
 int e=!c->enabled?I9100_CAM_DISABLED:c->fault?I9100_CAM_IO:m5read(c,category,reg,width,value);
 if(e==I9100_CAM_IO||e==I9100_CAM_FRAME)c->fault=1;c->io.release(c->io.cookie);return e;
}
int i9100_m5mo_set_parameter_mode(struct i9100_camera *c)
{
 if(!c||!c->initialized||c->kind!=I9100_CAMERA_M5MO)return I9100_CAM_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_CAM_BUSY;
 int e=0;if(!c->enabled){e=I9100_CAM_DISABLED;goto done;}if(c->fault){e=I9100_CAM_IO;goto done;}
 uint32_t mode=0;e=m5read(c,0,0x0b,1,&mode);if(e)goto done;
 if(mode==1U)goto done;if(mode!=2U){e=I9100_CAM_BUSY;goto done;}
 uint8_t bytes[5]={5,2,0,0x0b,1};struct i9100_i2c_message m={0x1f,0,5,bytes};e=transfer(c,&m,1);if(e)goto done;
 uint64_t started=c->io.now_ns(c->io.cookie);
 for(unsigned n=0;n<500U;n++){
  e=m5read(c,0,0x0b,1,&mode);if(e)break;if(mode==1U)break;
  uint64_t now=c->io.now_ns(c->io.cookie);if(n==499U||now<started||now-started>500000000ULL){e=I9100_CAM_TIMEOUT;break;}
  if(c->io.delay_us(c->io.cookie,1000)){e=I9100_CAM_IO;break;}
 }
done:
 if(e==I9100_CAM_IO||e==I9100_CAM_TIMEOUT||e==I9100_CAM_FRAME)c->fault=1;c->io.release(c->io.cookie);return e;
}
int i9100_s5k5bafx_read(struct i9100_camera *c,uint16_t page,uint16_t address,uint16_t *value)
{
 if(!c||!c->initialized||c->kind!=I9100_CAMERA_S5K5BAFX||!value||(page!=0xd000U&&page!=0x7000U)||(address&1U))return I9100_CAM_ARGUMENT;
 if(c->io.claim(c->io.cookie))return I9100_CAM_BUSY;
 int e=!c->enabled?I9100_CAM_DISABLED:c->fault?I9100_CAM_IO:s5read(c,page,address,value);
 if(e==I9100_CAM_IO)c->fault=1;c->io.release(c->io.cookie);return e;
}
