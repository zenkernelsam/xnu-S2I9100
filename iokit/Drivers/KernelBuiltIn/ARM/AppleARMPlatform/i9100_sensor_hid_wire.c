/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sensor_hid_wire.h"
#include <string.h>
static void le32(uint8_t *p,uint32_t n)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8U));}
static void le64(uint8_t *p,uint64_t n)
{le32(p,(uint32_t)n);le32(p+4,(uint32_t)(n>>32));}
int i9100_sensor_hid_usage(uint32_t kind,uint32_t *type,uint32_t *page,uint32_t *usage)
{
 if(!type||!page||!usage||type==page||type==usage||page==usage||kind<1U||kind>3U)return I9100_SH_ARGUMENT;
 static const uint32_t t[3]={13,20,21},u[3]={3,9,10};
 *type=t[kind-1U];*page=0xff00U;*usage=u[kind-1U];return 0;
}
static int fixed(uint32_t kind,int32_t value,int32_t *out)
{
 int64_t result;
 if(kind==I9100_SENSOR_K3DH)result=(int64_t)value*65536LL/1000000LL;
 else if(kind==I9100_SENSOR_K3G)result=(int64_t)value*1143818979LL/1000000000000LL;
 else result=(int64_t)value*65536LL/1000LL;
 if(result<(-2147483647LL-1LL)||result>2147483647LL)return I9100_SH_RANGE;
 *out=(int32_t)result;return 0;
}
int i9100_sensor_hid_encode(const struct i9100_sensor_sample *sample,uint64_t ticks,
 uint64_t sender,struct i9100_sensor_hid_packet *out)
{
 if(!sample||!out||!ticks||!sender||!sample->timestamp_ns||!sample->sequence||(sample->status&~255U)||
    sample->kind<1U||sample->kind>3U)return I9100_SH_ARGUMENT;
 if(sample->kind==I9100_SENSOR_AK8975){if(!(sample->status&1U)||(sample->status&2U))return I9100_SH_ARGUMENT;}
 else if(!(sample->status&8U)||(sample->status&128U))return I9100_SH_ARGUMENT;
 uintptr_t a=(uintptr_t)sample,b=(uintptr_t)out;
 if(a>~(uintptr_t)0-sizeof(*sample)||b>~(uintptr_t)0-sizeof(*out)||
    (a+sizeof(*sample)>b&&b+sizeof(*out)>a))return I9100_SH_ARGUMENT;
 int32_t axis[3];for(unsigned n=0;n<3;n++){int e=fixed(sample->kind,sample->xyz[n],&axis[n]);if(e)return e;}
 uint32_t type=0,page=0,usage=0;int e=i9100_sensor_hid_usage(sample->kind,&type,&page,&usage);if(e)return e;
 struct i9100_sensor_hid_packet packet;memset(&packet,0,sizeof(packet));
 le64(packet.bytes,ticks);le32(packet.bytes+12,1);le32(packet.bytes+16,44);le32(packet.bytes+20,type);
 le64(packet.bytes+40,sender);for(unsigned n=0;n<3;n++)le32(packet.bytes+48+n*4U,(uint32_t)axis[n]);
 memcpy(out,&packet,sizeof(packet));return 0;
}
