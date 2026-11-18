/* SPDX-License-Identifier: BSD-2-Clause; actual pdev to gated mode and ISA VM. */
#define I9100_GATED_USER_ENTRY unused_gated_user_reference
#include "i9100_audio_gated_user_test.cpp"
#include "../userland/audio/i9100-pdev.h"
static IOService *pdev_service;static IOUserClient *pdev_client;
static uint32_t pdev_device,pdev_stream,pdev_next_id=900,plugin_enabled=1;
static unsigned pdev_rpc,pdev_mutex,pdev_strings;
struct GatedCF {unsigned refs;char text[64];};
extern "C" const void *CFStringCreateWithCString(void *,const char *text,uint32_t)
{auto *p=(GatedCF *)calloc(1,sizeof(GatedCF));assert(strlen(text)<64);strcpy(p->text,text);p->refs=1;pdev_strings++;return p;}
extern "C" const void *CFRetain(const void *s){((GatedCF *)s)->refs++;return s;}
extern "C" void CFRelease(const void *s){auto *p=(GatedCF *)s;assert(p->refs);if(!--p->refs){pdev_strings--;free(p);}}
extern "C" const void *CFBundleGetBundleWithIdentifier(const void *){return (void *)1;}
extern "C" const void *CFBundleGetValueForInfoDictionaryKey(const void *,const void *){return &plugin_enabled;}
extern "C" uintptr_t CFGetTypeID(const void *){return 2;}
extern "C" uintptr_t CFBooleanGetTypeID(){return 2;}
extern "C" uint8_t CFBooleanGetValue(const void *){return plugin_enabled!=0;}
extern "C" int __wrap_pthread_mutex_init(void *,const void *){return 0;}
extern "C" int __wrap_pthread_mutex_lock(void *){assert(!pdev_mutex);pdev_mutex=1;return 0;}
extern "C" int __wrap_pthread_mutex_unlock(void *){assert(pdev_mutex);pdev_mutex=0;return 0;}
extern "C" {uint32_t mach_task_self_=1;}
extern "C" int mach_timebase_info(void *v){uint32_t t[2]={1,1};memcpy(v,t,8);return 0;}
extern "C" void *IOServiceMatching(const char *name){assert(!strcmp(name,I9100_AUDIO_USER_CLASS));return (void *)1;}
extern "C" uint32_t IOServiceGetMatchingService(uint32_t,void *){return pdev_service?1U:0U;}
extern "C" int IOObjectRelease(uint32_t){return 0;}
extern "C" int IOServiceOpen(uint32_t,uint32_t,uint32_t,uint32_t *port)
{int e=pdev_service->newUserClient((task_t)1,nullptr,0,nullptr,&pdev_client);if(!e)*port=7;return e;}
extern "C" int IOServiceClose(uint32_t)
{assert(pdev_client);int e=pdev_client->clientClose();pdev_client->stop(pdev_service);pdev_client->detach(pdev_service);pdev_client->release();pdev_client=nullptr;return e;}
extern "C" int IOConnectCallStructMethod(uint32_t,uint32_t selector,const void *input,size_t bytes,void *out,size_t *out_bytes)
{pdev_rpc++;IOExternalMethodArguments args;args.structureInput=input;args.structureInputSize=(uint32_t)bytes;
 args.structureOutput=out;args.structureOutputSize=(uint32_t)*out_bytes;int e=pdev_client->externalMethod(selector,&args,nullptr,nullptr,nullptr);
 *out_bytes=args.structureOutputSize;return e;}
extern "C" I9AudioStatus AudioObjectCreate(I9AudioObjectID,I9AudioObjectID owner,uint32_t kind,I9AudioObjectID *id)
{*id=pdev_next_id++;if(owner==1){assert(kind==I9_AUDIO_FOURCC('r','d','e','v'));pdev_device=*id;}
 else {assert(owner==pdev_device&&kind==I9_AUDIO_FOURCC('a','s','t','r'));pdev_stream=*id;}return 0;}
static I9AudioStatus pdev_changed(i9_audio_host *,I9AudioObjectID,uint32_t,const i9_audio_address *){assert(!pdev_mutex);return 0;}
static I9AudioStatus pdev_publish(i9_audio_host *,uint32_t count,const I9AudioObjectID *id)
{assert(!pdev_mutex&&count==1&&*id==pdev_device);return 0;}
static I9AudioStatus pdev_remove(i9_audio_host *,uint32_t count,const I9AudioObjectID *id)
{assert(!pdev_mutex&&count==1&&*id==pdev_device);return 0;}
static I9AudioStatus pdev_streams(i9_audio_host *,I9AudioObjectID device,uint32_t,const I9AudioObjectID *,uint32_t,const I9AudioObjectID *)
{assert(!pdev_mutex&&device==pdev_device);return 0;}
static i9_audio_host pdev_host={pdev_changed,pdev_publish,pdev_remove,0,42,nullptr,pdev_streams};
int main()
{
 IOService platform;IOS6I9100AudioPCM *device=nullptr;i9100_audio_board_native *board=nullptr;
 gated_board_setup(&board);i9100_audio_user_config cfg={48000,64,1536000,250,0,0,1};
 assert(!i9100_audio_user_device_create(1,&platform,board,nullptr,&cfg,&device));pdev_service=reinterpret_cast<IOService *>(device);
 plugin_enabled=0;assert(PhysicalDevicePlugIn_Initialize(&pdev_host)==I9_AUDIO_UNSUPPORTED&&!pdev_client&&!pdev_strings);
 plugin_enabled=1;assert(!PhysicalDevicePlugIn_Initialize(&pdev_host));
 i9_audio_address a={I9_AUDIO_FOURCC('g','o','i','n'),I9_AUDIO_GLOBAL,0};uint32_t on=1;
 assert(!PhysicalDevicePlugIn_ObjectSetPropertyData(pdev_device,&a,0,nullptr,4,&on));m.extra_contexts=1;
 void *buffer=nullptr;assert(!PhysicalDevicePlugIn_AcquireIOBufferForStream(pdev_device,pdev_stream,64,0,0,64,&buffer));
 ((int16_t *)buffer)[0]=32767;((int16_t *)buffer)[1]=-32768;
 assert(!PhysicalDevicePlugIn_WriteToStream(pdev_device,pdev_stream,64,0,0,64,buffer));unsigned calls=pdev_rpc;
 assert(PhysicalDevicePlugIn_WriteToStream(pdev_device,pdev_stream,64,0,0,64,buffer)==I9_AUDIO_UNSUPPORTED&&pdev_rpc==calls);
 assert(!PhysicalDevicePlugIn_ReleaseIOBufferForStream(pdev_device,pdev_stream,64,0,0,64,buffer));
 assert((m.codec[4][89]&127U)==0&&vm_state.words==64);
 m.ns+=1000000ULL;last_loop->tick();double sample=-1;uint64_t ticks=0,seed=0;
 assert(!PhysicalDevicePlugIn_GetZeroTimeStamp(pdev_device,&sample,&ticks,&seed)&&sample==64&&seed==1&&ticks);
 assert(!PhysicalDevicePlugIn_AcquireIOBufferForStream(pdev_device,pdev_stream,64,0,0,64,&buffer));
 ((int16_t *)buffer)[0]=32767;((int16_t *)buffer)[1]=-32768;
 assert(!PhysicalDevicePlugIn_WriteToStream(pdev_device,pdev_stream,64,0,0,64,buffer));
 assert((m.codec[4][89]&127U)==66U&&(m.codec[2][37]&31U)==1U);
 assert(!PhysicalDevicePlugIn_ReleaseIOBufferForStream(pdev_device,pdev_stream,64,0,0,64,buffer));
 m.ns+=1000000ULL;last_loop->tick();m.ns+=1000000ULL;last_loop->tick();
 assert(vm_state.words==256&&fifo_words[192]==((uint32_t)(uint16_t)-1036<<16|1035U));
 for(unsigned n=0;n<192;n++)assert(fifo_words[n]==0);
 assert(!PhysicalDevicePlugIn_GetZeroTimeStamp(pdev_device,&sample,&ticks,&seed)&&sample==192);
 on=0;assert(!PhysicalDevicePlugIn_ObjectSetPropertyData(pdev_device,&a,0,nullptr,4,&on));
 assert(!PhysicalDevicePlugIn_Teardown()&&!pdev_client&&!pdev_strings&&!pdev_mutex&&!m.dma_count);
 assert(!i9100_audio_user_device_destroy(&device)&&!event_objects);m.extra_contexts=0;
 assert(!i9100_audio_board_close(&board));board_released();
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 puts("PASS actual pdev -> gated user-client -> board/workloop/codec/I2S -> ISA VM: silent pre-roll, -30/-36dB, exact buffer/count/timestamp, no replay, stop");return 0;
}
