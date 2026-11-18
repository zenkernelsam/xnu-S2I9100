/* SPDX-License-Identifier: BSD-2-Clause
 * Real pdev C -> real IOUserClient -> real workloop/PCM/codec, with explicit
 * CoreAudio/CF/IOKit/board/MMIO fixtures. No phone or acoustic evidence.
 */
#define I9100_USER_MODEL_ENTRY baseline_user_client_suite
#include "i9100_audio_user_native_test.cpp"
#include "../userland/audio/i9100-pdev.h"
static IOService *hal_service;static IOUserClient *hal_client;
static unsigned bundle_on,cf_allocations,cf_fail_at,cf_live,matching_calls,rpc_calls,close_calls;
static unsigned rpc_fail_before,rpc_fail_after;
static uint32_t device_id,stream_id,next_object=700;
static unsigned publish_calls,remove_calls,stream_publish,stream_remove;
static unsigned mutex_depth;
struct CFModel {unsigned refs;char text[64];};
extern "C" const void *CFStringCreateWithCString(void *,const char *text,uint32_t encoding)
{
 assert(encoding==0x08000100U);if(++cf_allocations==cf_fail_at)return nullptr;
 auto *s=(CFModel *)calloc(1,sizeof(CFModel));assert(strlen(text)<64);strcpy(s->text,text);s->refs=1;cf_live++;return s;
}
extern "C" const void *CFRetain(const void *s){((CFModel *)s)->refs++;return s;}
extern "C" void CFRelease(const void *s){auto *p=(CFModel *)s;assert(p->refs);if(!--p->refs){cf_live--;free(p);}}
extern "C" const void *CFBundleGetBundleWithIdentifier(const void *s)
{assert(!strcmp(((CFModel *)s)->text,"org.i9100.audio.PCM"));return (const void *)0x11;}
extern "C" const void *CFBundleGetValueForInfoDictionaryKey(const void *,const void *s)
{assert(!strcmp(((CFModel *)s)->text,"I9100AudioPDevEnabled"));return &bundle_on;}
extern "C" uintptr_t CFGetTypeID(const void *s){assert(s==&bundle_on);return 2;}
extern "C" uintptr_t CFBooleanGetTypeID(){return 2;}
extern "C" uint8_t CFBooleanGetValue(const void *){return bundle_on!=0;}
extern "C" int __wrap_pthread_mutex_init(void *,const void *){return 0;}
extern "C" int __wrap_pthread_mutex_lock(void *){assert(!mutex_depth);mutex_depth=1;return 0;}
extern "C" int __wrap_pthread_mutex_unlock(void *){assert(mutex_depth);mutex_depth=0;return 0;}
extern "C" {uint32_t mach_task_self_=1;}
extern "C" int mach_timebase_info(void *p){uint32_t t[2]={3,5};memcpy(p,t,8);return 0;}
extern "C" void *IOServiceMatching(const char *name)
{assert(!strcmp(name,I9100_AUDIO_USER_CLASS));matching_calls++;return (void *)1;}
extern "C" uint32_t IOServiceGetMatchingService(uint32_t,void *){return hal_service?1U:0U;}
extern "C" int IOObjectRelease(uint32_t service){assert(service==1);return 0;}
extern "C" int IOServiceOpen(uint32_t service,uint32_t task,uint32_t type,uint32_t *port)
{
 assert(service==1&&task==1&&!type&&!hal_client);int e=hal_service->newUserClient((task_t)1,nullptr,0,nullptr,&hal_client);
 if(!e)*port=7;return e;
}
extern "C" int IOServiceClose(uint32_t port)
{
 assert(port==7&&hal_client);close_calls++;int e=hal_client->clientClose();
 close_client(hal_client,hal_service);hal_client=nullptr;return e;
}
extern "C" int IOConnectCallStructMethod(uint32_t port,uint32_t selector,const void *input,size_t bytes,void *out,size_t *out_bytes)
{
 assert(port==7&&hal_client&&bytes>=32&&*out_bytes==128);rpc_calls++;
 auto *q=(const i9100_audio_user_request *)input;if(q->op==rpc_fail_before)return 31;
 IOExternalMethodArguments a;a.structureInput=input;a.structureInputSize=(uint32_t)bytes;
 a.structureOutput=out;a.structureOutputSize=(uint32_t)*out_bytes;
 int e=hal_client->externalMethod(selector,&a,nullptr,nullptr,nullptr);*out_bytes=a.structureOutputSize;
 return q->op==rpc_fail_after?31:e;
}
extern "C" I9AudioStatus AudioObjectCreate(I9AudioObjectID plugin,I9AudioObjectID owner,uint32_t kind,I9AudioObjectID *out)
{
 assert(plugin==42);*out=next_object++;
 if(kind==I9_AUDIO_FOURCC('r','d','e','v')){assert(owner==1);device_id=*out;}
 else {assert(kind==I9_AUDIO_FOURCC('a','s','t','r')&&owner==device_id);stream_id=*out;}
 return 0;
}
static I9AudioStatus changed(i9_audio_host *,I9AudioObjectID object,uint32_t count,const i9_audio_address *)
{
 assert(!mutex_depth&&object==device_id&&count==1);uint32_t value=9,bytes=4;
 i9_audio_address a={I9_AUDIO_FOURCC('g','o','i','n'),I9_AUDIO_GLOBAL,0};
 assert(!PhysicalDevicePlugIn_ObjectGetPropertyData(object,&a,0,nullptr,&bytes,&value));
 assert(PhysicalDevicePlugIn_Teardown()==I9_AUDIO_UNSUPPORTED);return 0;
}
static I9AudioStatus published(i9_audio_host *,uint32_t count,const I9AudioObjectID *objects)
{
 assert(!mutex_depth&&count==1&&objects[0]==device_id);publish_calls++;
 i9_audio_address a={I9_AUDIO_FOURCC('l','n','a','m'),I9_AUDIO_GLOBAL,0};
 const void *name=nullptr;uint32_t bytes=sizeof(name);
 assert(!PhysicalDevicePlugIn_ObjectGetPropertyData(objects[0],&a,0,nullptr,&bytes,&name));
 assert(!strcmp(((CFModel *)name)->text,"I9100 PCM"));CFRelease(name);return 0;
}
static I9AudioStatus removed(i9_audio_host *,uint32_t count,const I9AudioObjectID *objects)
{assert(!mutex_depth&&count==1&&objects[0]==device_id);remove_calls++;return 0;}
static I9AudioStatus streams(i9_audio_host *,I9AudioObjectID device,uint32_t added,const I9AudioObjectID *adds,
 uint32_t removed_count,const I9AudioObjectID *removes)
{
 assert(!mutex_depth&&device==device_id);
 if(added){assert(added==1&&adds[0]==stream_id&&!removed_count);stream_publish++;
  i9_audio_address a={I9_AUDIO_FOURCC('s','f','m','t'),I9_AUDIO_GLOBAL,0};i9_audio_format f={};uint32_t bytes=40;
  assert(!PhysicalDevicePlugIn_ObjectGetPropertyData(stream_id,&a,0,nullptr,&bytes,&f));
  assert(f.sample_rate==48000&&f.channels==2&&f.bytes_frame==4&&f.bits==16&&f.flags==12);
 }else {assert(removed_count==1&&removes[0]==stream_id);stream_remove++;}return 0;
}
static i9_audio_host host={changed,published,removed,0,42,nullptr,streams};
static void begin_device(IOService *platform,IOS6I9100AudioPCM **device,i9100_audio_board_native **board,i9100_irq_registry *registry,int audible)
{
 setup_worker(board,registry);m.extra_contexts=0;i9100_audio_user_config config={48000,64,1536000,250,0,0,audible};
 assert(!i9100_audio_user_device_create(1,platform,*board,registry,&config,device));
 hal_service=reinterpret_cast<IOService *>(*device);hal_client=nullptr;
 rpc_fail_before=rpc_fail_after=0xffffffffU;publish_calls=remove_calls=stream_publish=stream_remove=0;
 rpc_calls=close_calls=0;assert(!cf_live&&!mutex_depth);
}
static I9AudioStatus running(uint32_t value)
{i9_audio_address a={I9_AUDIO_FOURCC('g','o','i','n'),I9_AUDIO_GLOBAL,0};return PhysicalDevicePlugIn_ObjectSetPropertyData(device_id,&a,0,nullptr,4,&value);}
static void end_hal(IOS6I9100AudioPCM **device,i9100_audio_board_native **board)
{
 assert(!PhysicalDevicePlugIn_Teardown()&&!hal_client&&!cf_live&&!mutex_depth);
 assert(publish_calls==1&&remove_calls==1&&stream_publish==1&&stream_remove==1);
 end_device(device,board);hal_service=nullptr;
}
int main()
{
 IOService platform;IOS6I9100AudioPCM *device=nullptr;
 i9100_audio_board_native *board=nullptr;i9100_irq_registry registry;
 begin_device(&platform,&device,&board,&registry,0);bundle_on=0;unsigned calls=matching_calls;
 assert(PhysicalDevicePlugIn_Initialize(&host)==I9_AUDIO_UNSUPPORTED&&matching_calls==calls&&!cf_live);
 bundle_on=1;assert(!PhysicalDevicePlugIn_Initialize(&host)&&publish_calls==1&&stream_publish==1);
 i9_audio_address format_address={I9_AUDIO_FOURCC('s','f','m','t'),I9_AUDIO_GLOBAL,0};uint32_t bytes=39;
 uint8_t guarded[42];memset(guarded,0xcc,sizeof(guarded));
 assert(PhysicalDevicePlugIn_ObjectGetPropertyData(stream_id,&format_address,0,nullptr,&bytes,guarded+1)==I9_AUDIO_BAD_SIZE);
 for(auto value:guarded)assert(value==0xcc);
 assert(!running(1));m.extra_contexts=1;
 double sample=-1;uint64_t ticks=0,seed=0;assert(!PhysicalDevicePlugIn_GetZeroTimeStamp(device_id,&sample,&ticks,&seed)&&sample==0&&seed==1&&ticks);
 uint64_t start=ticks;void *buffer=nullptr;
 assert(!PhysicalDevicePlugIn_BeginIOCycle(device_id,0,64,0,0,64));
 assert(!PhysicalDevicePlugIn_AcquireIOBufferForStream(device_id,stream_id,64,0,0,64,&buffer));
 ((int16_t *)buffer)[0]=32767;((int16_t *)buffer)[1]=-32768;
 assert(!PhysicalDevicePlugIn_WriteToStream(device_id,stream_id,64,0,0,64,buffer));
 calls=rpc_calls;assert(PhysicalDevicePlugIn_WriteToStream(device_id,stream_id,64,0,0,64,buffer)==I9_AUDIO_UNSUPPORTED&&rpc_calls==calls);
 assert(((int16_t *)allocation_bytes[1])[0]==1035&&((int16_t *)allocation_bytes[1])[1]==-1036);
 assert(!PhysicalDevicePlugIn_ReleaseIOBufferForStream(device_id,stream_id,64,0,0,64,buffer));
 m.ns+=2000000;m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();
 assert(!PhysicalDevicePlugIn_GetZeroTimeStamp(device_id,&sample,&ticks,&seed)&&sample==64&&ticks>start&&seed==1);
 uint64_t old=ticks;last_loop->tick();assert(!PhysicalDevicePlugIn_GetZeroTimeStamp(device_id,&sample,&ticks,&seed)&&sample==64&&ticks==old);
 assert(!running(0));assert(!running(1));m.extra_contexts=1;
 assert(!PhysicalDevicePlugIn_GetZeroTimeStamp(device_id,&sample,&ticks,&seed)&&sample==0&&seed==2);
 end_hal(&device,&board);
 /* Gated low output uses the real kernel's completed silence and native
  * qualification. The source amplitudes still pass through its -30dB cap. */
 begin_device(&platform,&device,&board,&registry,1);assert(!PhysicalDevicePlugIn_Initialize(&host));assert(!running(1));m.extra_contexts=1;
 m.ns+=2000000;m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();m.pdma[0x100/4]=1;
 assert(!PhysicalDevicePlugIn_AcquireIOBufferForStream(device_id,stream_id,64,0,0,64,&buffer));
 ((int16_t *)buffer)[0]=32767;((int16_t *)buffer)[1]=-32768;
 assert(!PhysicalDevicePlugIn_WriteToStream(device_id,stream_id,64,0,0,64,buffer));
 assert((m.codec[4][89]&0x7fU)==66U&&(m.codec[2][37]&0x1fU)==1U);
 assert(PhysicalDevicePlugIn_Teardown()==I9_AUDIO_UNSUPPORTED&&!hal_client);
 assert(PhysicalDevicePlugIn_WriteToStream(device_id,stream_id,64,0,0,64,buffer)==I9_AUDIO_UNSUPPORTED);
 assert(!PhysicalDevicePlugIn_ReleaseIOBufferForStream(device_id,stream_id,64,0,0,64,buffer));end_hal(&device,&board);
 /* Lose the RPC acknowledgement after the actual kernel accepted a block.
  * Close stops the real worker without sequence retry, and a second write
  * of that same buffer is refused. */
 begin_device(&platform,&device,&board,&registry,0);assert(!PhysicalDevicePlugIn_Initialize(&host));assert(!running(1));m.extra_contexts=1;
 assert(!PhysicalDevicePlugIn_AcquireIOBufferForStream(device_id,stream_id,64,0,0,64,&buffer));rpc_fail_after=I9100_AU_ENQUEUE;
 assert(PhysicalDevicePlugIn_WriteToStream(device_id,stream_id,64,0,0,64,buffer)==I9_AUDIO_ERROR&&!hal_client&&close_calls==1);
 calls=rpc_calls;assert(PhysicalDevicePlugIn_WriteToStream(device_id,stream_id,64,0,0,64,buffer)==I9_AUDIO_UNSUPPORTED&&rpc_calls==calls);
 assert(!PhysicalDevicePlugIn_ReleaseIOBufferForStream(device_id,stream_id,64,0,0,64,buffer));end_hal(&device,&board);
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 puts("PASS actual pdev/user-client/worker/PCM chain with host facades: default OFF/reentrant publication/signed16/low gain/actual completion clock/no replay/late release/close");return 0;
}
