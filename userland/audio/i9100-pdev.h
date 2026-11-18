/* SPDX-License-Identifier: BSD-2-Clause
 * Original iOS6 CoreAudio UUID 5d534dbf76ff30f4a628f25f56c5f26a.
 * Consumer thunks 30e8bc84..30e8c014 strip the C++ this pointer;
 * property read carries UInt32 *ioSize, stream buffers are the seventh arg.
 */
#ifndef I9100_PDEV_H
#define I9100_PDEV_H
#include <stdint.h>
#include <stddef.h>
#include "i9100_audio_user.h"
typedef uint32_t I9AudioObjectID;
typedef int32_t I9AudioStatus;
struct i9_audio_address {uint32_t selector,scope,element;};
struct i9_audio_format {
 double sample_rate;
 uint32_t format,flags,bytes_packet,frames_packet,bytes_frame,channels,bits,reserved;
};
struct i9_audio_ranged_format {struct i9_audio_format format;double minimum,maximum;};
struct i9_audio_host {
 I9AudioStatus (*properties_changed)(struct i9_audio_host *,I9AudioObjectID,uint32_t,const struct i9_audio_address *);
 I9AudioStatus (*publish_devices)(struct i9_audio_host *,uint32_t,const I9AudioObjectID *);
 I9AudioStatus (*remove_devices)(struct i9_audio_host *,uint32_t,const I9AudioObjectID *);
 uintptr_t unused_config_request;
 I9AudioObjectID plugin_id;
 void *context;
 I9AudioStatus (*streams_changed)(struct i9_audio_host *,I9AudioObjectID,uint32_t,const I9AudioObjectID *,uint32_t,const I9AudioObjectID *);
};
#define I9_AUDIO_FOURCC(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))
#define I9_AUDIO_GLOBAL I9_AUDIO_FOURCC('g','l','o','b')
#define I9_AUDIO_OUTPUT I9_AUDIO_FOURCC('o','u','t','p')
#define I9_AUDIO_INPUT I9_AUDIO_FOURCC('i','n','p','t')
#define I9_AUDIO_BAD_PROPERTY ((I9AudioStatus)0x77686f3f)
#define I9_AUDIO_BAD_SIZE ((I9AudioStatus)0x2173697a)
#define I9_AUDIO_BAD_OBJECT ((I9AudioStatus)0x216f626a)
#define I9_AUDIO_UNSUPPORTED ((I9AudioStatus)0x756e6f70)
#define I9_AUDIO_ERROR ((I9AudioStatus)0x77686174)
#ifdef __cplusplus
extern "C" {
#endif
I9AudioStatus PhysicalDevicePlugIn_Initialize(struct i9_audio_host *);
I9AudioStatus PhysicalDevicePlugIn_Teardown(void);
I9AudioStatus PhysicalDevicePlugIn_PerformDeviceConfigChange(I9AudioObjectID,uint32_t);
uint8_t PhysicalDevicePlugIn_ObjectHasProperty(I9AudioObjectID,const struct i9_audio_address *);
I9AudioStatus PhysicalDevicePlugIn_ObjectIsPropertySettable(I9AudioObjectID,const struct i9_audio_address *,uint8_t *);
I9AudioStatus PhysicalDevicePlugIn_ObjectGetPropertyDataSize(I9AudioObjectID,const struct i9_audio_address *,uint32_t,const void *,uint32_t *);
I9AudioStatus PhysicalDevicePlugIn_ObjectGetPropertyData(I9AudioObjectID,const struct i9_audio_address *,uint32_t,const void *,uint32_t *,void *);
I9AudioStatus PhysicalDevicePlugIn_ObjectSetPropertyData(I9AudioObjectID,const struct i9_audio_address *,uint32_t,const void *,uint32_t,const void *);
I9AudioStatus PhysicalDevicePlugIn_GetZeroTimeStamp(I9AudioObjectID,double *,uint64_t *,uint64_t *);
I9AudioStatus PhysicalDevicePlugIn_BeginIOCycle(I9AudioObjectID,uint32_t,uint32_t,uint32_t,double,double);
I9AudioStatus PhysicalDevicePlugIn_EndIOCycle(I9AudioObjectID,uint32_t,uint32_t,uint32_t,double,double);
I9AudioStatus PhysicalDevicePlugIn_AcquireIOBufferForStream(I9AudioObjectID,I9AudioObjectID,uint32_t,uint32_t,double,double,void **);
I9AudioStatus PhysicalDevicePlugIn_ReleaseIOBufferForStream(I9AudioObjectID,I9AudioObjectID,uint32_t,uint32_t,double,double,void *);
I9AudioStatus PhysicalDevicePlugIn_ReadFromStream(I9AudioObjectID,I9AudioObjectID,uint32_t,uint32_t,double,double,void *);
I9AudioStatus PhysicalDevicePlugIn_WriteToStream(I9AudioObjectID,I9AudioObjectID,uint32_t,uint32_t,double,double,void *);
#ifdef __cplusplus
}
#endif
#endif
