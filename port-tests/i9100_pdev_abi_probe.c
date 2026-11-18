/* SPDX-License-Identifier: BSD-2-Clause; compiler ABI oracle, never a driver. */
#include "../userland/audio/i9100-pdev.h"
volatile struct {uint32_t words[8],times[4];} i9100_pdev_abi_record;
static void save(uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{i9100_pdev_abi_record.words[0]=a;i9100_pdev_abi_record.words[1]=b;i9100_pdev_abi_record.words[2]=c;i9100_pdev_abi_record.words[3]=d;}
static void cycle(uint32_t a,uint32_t b,uint32_t c,uint32_t d,double first,double last)
{
 union {double value;uint32_t words[2];} x,y;x.value=first;y.value=last;save(a,b,c,d);
 i9100_pdev_abi_record.times[0]=x.words[0];i9100_pdev_abi_record.times[1]=x.words[1];
 i9100_pdev_abi_record.times[2]=y.words[0];i9100_pdev_abi_record.times[3]=y.words[1];
}
I9AudioStatus PhysicalDevicePlugIn_Initialize(struct i9_audio_host *h){save((uint32_t)(uintptr_t)h,0,0,0);return 0;}
I9AudioStatus PhysicalDevicePlugIn_Teardown(void){save(0x54454152,0,0,0);return 0;}
I9AudioStatus PhysicalDevicePlugIn_PerformDeviceConfigChange(I9AudioObjectID d,uint32_t a){save(d,a,0,0);return 0;}
uint8_t PhysicalDevicePlugIn_ObjectHasProperty(I9AudioObjectID d,const struct i9_audio_address *p){save(d,(uint32_t)(uintptr_t)p,0,0);return 1;}
I9AudioStatus PhysicalDevicePlugIn_ObjectIsPropertySettable(I9AudioObjectID d,const struct i9_audio_address *p,uint8_t *v)
{save(d,(uint32_t)(uintptr_t)p,(uint32_t)(uintptr_t)v,0);*v=1;return 0;}
I9AudioStatus PhysicalDevicePlugIn_ObjectGetPropertyDataSize(I9AudioObjectID d,const struct i9_audio_address *p,uint32_t n,const void *q,uint32_t *v)
{save(d,(uint32_t)(uintptr_t)p,n,(uint32_t)(uintptr_t)q);i9100_pdev_abi_record.words[4]=(uint32_t)(uintptr_t)v;*v=0x5a;return 0;}
I9AudioStatus PhysicalDevicePlugIn_ObjectGetPropertyData(I9AudioObjectID d,const struct i9_audio_address *p,uint32_t n,const void *q,uint32_t *v,void *data)
{save(d,(uint32_t)(uintptr_t)p,n,(uint32_t)(uintptr_t)q);i9100_pdev_abi_record.words[4]=(uint32_t)(uintptr_t)v;
 i9100_pdev_abi_record.words[5]=(uint32_t)(uintptr_t)data;*v=4;*(uint32_t *)data=0x12345678;return 0;}
I9AudioStatus PhysicalDevicePlugIn_ObjectSetPropertyData(I9AudioObjectID d,const struct i9_audio_address *p,uint32_t n,const void *q,uint32_t v,const void *data)
{save(d,(uint32_t)(uintptr_t)p,n,(uint32_t)(uintptr_t)q);i9100_pdev_abi_record.words[4]=v;i9100_pdev_abi_record.words[5]=(uint32_t)(uintptr_t)data;return 0;}
I9AudioStatus PhysicalDevicePlugIn_GetZeroTimeStamp(I9AudioObjectID d,double *s,uint64_t *t,uint64_t *seed)
{save(d,(uint32_t)(uintptr_t)s,(uint32_t)(uintptr_t)t,(uint32_t)(uintptr_t)seed);*s=123.0;*t=0xabcdef9012345678ULL;*seed=44;return 0;}
#define CYCLE(name) I9AudioStatus PhysicalDevicePlugIn_##name(I9AudioObjectID d,uint32_t t,uint32_t n,uint32_t f,double b,double e) {cycle(d,t,n,f,b,e);return 0;}
CYCLE(BeginIOCycle)
CYCLE(EndIOCycle)
#define BUFFER(name) I9AudioStatus PhysicalDevicePlugIn_##name(I9AudioObjectID d,I9AudioObjectID s,uint32_t n,uint32_t f,double b,double e,void *v) {cycle(d,s,n,f,b,e);i9100_pdev_abi_record.words[4]=(uint32_t)(uintptr_t)v;return 0;}
BUFFER(ReleaseIOBufferForStream)
BUFFER(ReadFromStream)
BUFFER(WriteToStream)
I9AudioStatus PhysicalDevicePlugIn_AcquireIOBufferForStream(I9AudioObjectID d,I9AudioObjectID s,uint32_t n,uint32_t f,double b,double e,void **v)
{cycle(d,s,n,f,b,e);i9100_pdev_abi_record.words[4]=(uint32_t)(uintptr_t)v;*v=(void *)0xdeadbeef;return 0;}
