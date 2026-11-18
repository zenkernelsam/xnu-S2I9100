/* SPDX-License-Identifier: BSD-2-Clause; default-off original iOS6 pdev adapter. */
#include "i9100-pdev.h"
/* Public C ABIs, with opaque CF/IOKit references; no private SDK replacement. */
typedef const void *CFRef;typedef uint32_t I9Port;
extern CFRef CFStringCreateWithCString(void *,const char *,uint32_t);
extern CFRef CFRetain(CFRef);extern void CFRelease(CFRef);
extern CFRef CFBundleGetBundleWithIdentifier(CFRef);
extern CFRef CFBundleGetValueForInfoDictionaryKey(CFRef,CFRef);
extern uintptr_t CFGetTypeID(CFRef);extern uintptr_t CFBooleanGetTypeID(void);
extern uint8_t CFBooleanGetValue(CFRef);
extern void *IOServiceMatching(const char *);
extern I9Port IOServiceGetMatchingService(I9Port,void *);
extern int IOServiceOpen(I9Port,I9Port,uint32_t,I9Port *);
extern int IOObjectRelease(I9Port);extern int IOServiceClose(I9Port);
extern int IOConnectCallStructMethod(I9Port,uint32_t,const void *,size_t,void *,size_t *);
extern I9Port mach_task_self_;
extern int mach_timebase_info(void *);
extern I9AudioStatus AudioObjectCreate(I9AudioObjectID,I9AudioObjectID,uint32_t,I9AudioObjectID *);
extern int pthread_mutex_init(void *,const void *),pthread_mutex_lock(void *),pthread_mutex_unlock(void *);
extern void *memcpy(void *,const void *,size_t);extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
/* sys/_types.h: ARM32 long signature + 40 opaque bytes; LP64 uses 56.
 * Initialize once and retain until module unload, so late teardown callbacks
 * never race a destroyed mutex. Host publication callbacks run outside it.
 */
static struct {long signature;unsigned char opaque[sizeof(void *)==4?40:56];} mutex;
static uint32_t mutex_state;
static struct {
 struct i9_audio_host *host;I9Port connection;I9AudioObjectID device,stream;
 CFRef name,uid,maker;
 uint32_t initializing,initialized,closing,device_announced,stream_announced,callbacks,tearing_down;
 uint32_t session,sequence,running,fault,activation_attempted,buffer_owned,buffer_frames,buffer_submitted;
 struct i9100_audio_user_reply status;
 struct {uint32_t numer,denom;} timebase;
 int16_t buffer[I9100_AUDIO_USER_MAX_FRAMES*2U];
 int32_t last_error;
} g;
#define FCC(a,b,c,d) I9_AUDIO_FOURCC(a,b,c,d)
static I9AudioStatus lock(void)
{
 if(__sync_bool_compare_and_swap(&mutex_state,0,1)){
  int e=pthread_mutex_init(&mutex,0);(void)__sync_lock_test_and_set(&mutex_state,e?3U:2U);
 }
 if(__sync_val_compare_and_swap(&mutex_state,2U,2U)!=2U||pthread_mutex_lock(&mutex))return I9_AUDIO_ERROR;
 return 0;
}
static void unlock(void){(void)pthread_mutex_unlock(&mutex);}
static void close_connection(void)
{
 if(!g.connection)return;
 I9Port port=g.connection;g.connection=0;
 int e=IOServiceClose(port);if(e&&!g.last_error)g.last_error=e;
 g.running=0;g.fault=1; /* A failed close is retained by the kernel owner. */
}
static I9AudioStatus exchange(uint32_t op,uint32_t a,uint32_t b,const int16_t *samples,uint32_t frames)
{
 if(!g.connection||g.fault||frames>I9100_AUDIO_USER_MAX_FRAMES)return I9_AUDIO_UNSUPPORTED;
 struct {struct i9100_audio_user_request q;int16_t samples[I9100_AUDIO_USER_MAX_FRAMES*2U];} message;
 memset(&message.q,0,sizeof(message.q));message.q.abi=I9100_AUDIO_USER_ABI;message.q.op=op;
 if(op){if(!g.session||!g.sequence||g.sequence==0xffffffffU){close_connection();return I9_AUDIO_ERROR;}
  message.q.session=g.session;message.q.sequence=g.sequence++;}
 message.q.arg0=a;message.q.arg1=b;message.q.frames=frames;
 if(frames)memcpy(message.samples,samples,frames*4U);
 struct i9100_audio_user_reply reply;memset(&reply,0,sizeof(reply));size_t bytes=sizeof(reply);
 int e=IOConnectCallStructMethod(g.connection,0,&message,sizeof(message.q)+frames*4U,&reply,&bytes);
 if(e||bytes!=sizeof(reply)||reply.abi!=I9100_AUDIO_USER_ABI||reply.reserved||
    (op&&(reply.session!=g.session||reply.sequence!=message.q.sequence))||
    (!op&&(!reply.session||reply.sequence))||reply.max_frames!=I9100_AUDIO_USER_MAX_FRAMES||reply.channels!=2U||
    reply.error||reply.snapshot_error||(op==I9100_AU_ENQUEUE&&reply.accepted_frames!=frames)){
  g.last_error=e?e:reply.error?reply.error:reply.snapshot_error?reply.snapshot_error:I9100_AU_IO;
  close_connection();return I9_AUDIO_ERROR;
 }
 if(!op){g.session=reply.session;g.sequence=1;}
 g.status=reply;g.running=(reply.flags&I9100_AU_RUNNING)!=0;
 if(reply.flags&I9100_AU_FAULT){g.last_error=reply.last_error?reply.last_error:I9100_AU_IO;close_connection();return I9_AUDIO_ERROR;}
 return 0;
}
static int bundle_enabled(void)
{
 CFRef id=CFStringCreateWithCString(0,"org.i9100.audio.PCM",0x08000100U);
 CFRef key=CFStringCreateWithCString(0,"I9100AudioPDevEnabled",0x08000100U);
 int enabled=0;
 if(id&&key){CFRef bundle=CFBundleGetBundleWithIdentifier(id);
  CFRef flag=bundle?CFBundleGetValueForInfoDictionaryKey(bundle,key):0;
  enabled=flag&&CFGetTypeID(flag)==CFBooleanGetTypeID()&&CFBooleanGetValue(flag);}
 if(id)CFRelease(id);if(key)CFRelease(key);return enabled;
}
static int valid_object(I9AudioObjectID object,const struct i9_audio_address *address)
{return g.host&&object&&(object==g.device||object==g.stream)&&address&&address->element==0U&&
 (address->scope==I9_AUDIO_GLOBAL||address->scope==I9_AUDIO_INPUT||address->scope==I9_AUDIO_OUTPUT);}
static I9AudioStatus property_size(I9AudioObjectID object,const struct i9_audio_address *a,uint32_t *size)
{
 if(!valid_object(object,a))return I9_AUDIO_BAD_OBJECT;
 uint32_t n=0;
 switch(a->selector){
 case FCC('l','n','a','m'):case FCC('l','m','a','k'):case FCC('u','i','d',' '):case FCC('m','u','i','d'):n=sizeof(CFRef);break;
 case FCC('n','a','m','e'):n=9;break;
 case FCC('a','s','r','t'):case FCC('n','s','r','t'):n=8;break;
 case FCC('n','s','r','#'):case FCC('s','l','a','y'):n=16;break;
 case FCC('s','f','m','t'):case FCC('p','f','t',' '):n=40;break;
 case FCC('s','f','m','a'):case FCC('p','f','t','a'):n=56;break;
 case FCC('s','t','m','#'):n=object==g.device&&a->scope!=I9_AUDIO_INPUT?4:0;break;
 case FCC('o','w','n','d'):n=object==g.device?4:0;break;
 case FCC('d','c','h','2'):n=8;break;
 case FCC('c','l','a','s'):case FCC('b','c','l','s'):case FCC('p','l','u','g'):
 case FCC('g','o','i','n'):case FCC('g','o','n','e'):case FCC('l','i','v','n'):case FCC('h','i','d','n'):
 case FCC('f','s','i','z'):case FCC('s','d','i','r'):case FCC('t','e','r','m'):case FCC('t','r','a','n'):
 case FCC('s','a','c','t'):case FCC('s','c','h','n'):case FCC('l','t','n','c'):case FCC('c','l','k','d'):
 case FCC('d','f','l','t'):case FCC('m','u','t','e'):case FCC('o','i','n','k'):n=4;break;
 default:return I9_AUDIO_BAD_PROPERTY;
 }
 *size=n;return 0;
}
static struct i9_audio_format format(void)
{struct i9_audio_format value={(double)g.status.sample_rate,FCC('l','p','c','m'),12,4,1,4,2,16,0};return value;}
I9AudioStatus PhysicalDevicePlugIn_Initialize(struct i9_audio_host *host)
{
 if(!host||!host->plugin_id||!host->publish_devices||!host->remove_devices||!host->streams_changed)return I9_AUDIO_BAD_OBJECT;
 I9AudioStatus e=lock();if(e)return e;
 if(g.host||g.initializing||g.buffer_owned){unlock();return I9_AUDIO_UNSUPPORTED;}
 if(!bundle_enabled()){unlock();return I9_AUDIO_UNSUPPORTED;}
 memset(&g,0,sizeof(g));g.host=host;g.initializing=1;
 g.name=CFStringCreateWithCString(0,"I9100 PCM",0x08000100U);
 g.uid=CFStringCreateWithCString(0,"i9100-mc1n2-pcm-1",0x08000100U);
 g.maker=CFStringCreateWithCString(0,"I9100 development",0x08000100U);
 I9Port service=IOServiceGetMatchingService(0,IOServiceMatching(I9100_AUDIO_USER_CLASS));
 if(!g.name||!g.uid||!g.maker||!service)e=I9_AUDIO_UNSUPPORTED;
 if(!e&&IOServiceOpen(service,mach_task_self_,0,&g.connection))e=I9_AUDIO_ERROR;
 if(service)IOObjectRelease(service);
 if(!e)e=exchange(I9100_AU_INFO,0,0,0,0);
 if(!e&&(g.status.sample_rate!=44100U&&g.status.sample_rate!=48000U))e=I9_AUDIO_UNSUPPORTED;
 if(!e&&(g.status.period_frames<16U||g.status.period_frames>I9100_AUDIO_USER_MAX_FRAMES))e=I9_AUDIO_UNSUPPORTED;
 if(!e&&(mach_timebase_info(&g.timebase)||!g.timebase.numer||!g.timebase.denom))e=I9_AUDIO_ERROR;
 unlock();
 /* CoreAudio can synchronously query the new object's properties while
  * publishing it. Never enter a host callback with the pdev mutex held. */
 I9AudioObjectID device=0,stream=0;
 if(!e)e=AudioObjectCreate(host->plugin_id,1,FCC('r','d','e','v'),&device);
 if(!e)e=AudioObjectCreate(host->plugin_id,device,FCC('a','s','t','r'),&stream);
 if(lock())return I9_AUDIO_ERROR;g.device=device;g.stream=stream;unlock();
 if(!e){if(lock())return I9_AUDIO_ERROR;g.device_announced=1;unlock();e=host->publish_devices(host,1,&device);}
 if(!e){if(lock())return I9_AUDIO_ERROR;g.stream_announced=1;unlock();e=host->streams_changed(host,device,1,&stream,0,0);}
 if(lock())return I9_AUDIO_ERROR;g.initializing=0;g.initialized=e?0U:1U;unlock();
 if(e){I9AudioStatus cleanup=PhysicalDevicePlugIn_Teardown();return cleanup?cleanup:e;}return 0;
}
I9AudioStatus PhysicalDevicePlugIn_Teardown(void)
{
 I9AudioStatus e=lock();if(e)return e;
 if(!g.host){unlock();return 0;}if(g.initializing||g.callbacks||g.tearing_down){unlock();return I9_AUDIO_UNSUPPORTED;}
 g.closing=1;g.initialized=0;close_connection();
 if(g.buffer_owned){unlock();return I9_AUDIO_UNSUPPORTED;}
 struct i9_audio_host *host=g.host;I9AudioObjectID device=g.device,stream=g.stream;
 uint32_t stream_remove=g.stream_announced,device_remove=g.device_announced||device;
 g.tearing_down=1;
 unlock();
 if(stream_remove&&stream)e=host->streams_changed(host,device,0,0,1,&stream);
 if(!e&&device_remove)e=host->remove_devices(host,1,&device);
 if(lock())return I9_AUDIO_ERROR;
 if(e){g.tearing_down=0;unlock();return e;}
 CFRef name=g.name,uid=g.uid,maker=g.maker;memset(&g,0,sizeof(g));unlock();
 if(name)CFRelease(name);if(uid)CFRelease(uid);if(maker)CFRelease(maker);return 0;
}
I9AudioStatus PhysicalDevicePlugIn_PerformDeviceConfigChange(I9AudioObjectID device,uint32_t action)
{(void)device;(void)action;return I9_AUDIO_UNSUPPORTED;}
uint8_t PhysicalDevicePlugIn_ObjectHasProperty(I9AudioObjectID object,const struct i9_audio_address *address)
{if(lock())return 0;uint32_t size;I9AudioStatus e=property_size(object,address,&size);unlock();return e==0;}
I9AudioStatus PhysicalDevicePlugIn_ObjectIsPropertySettable(I9AudioObjectID object,const struct i9_audio_address *a,uint8_t *out)
{if(!out)return I9_AUDIO_BAD_SIZE;I9AudioStatus e=lock();if(e)return e;uint32_t size;e=property_size(object,a,&size);
 if(!e)*out=object==g.device&&a->selector==FCC('g','o','i','n');unlock();return e;}
I9AudioStatus PhysicalDevicePlugIn_ObjectGetPropertyDataSize(I9AudioObjectID object,const struct i9_audio_address *a,
 uint32_t qualifier_size,const void *qualifier,uint32_t *out)
{(void)qualifier_size;(void)qualifier;if(!out)return I9_AUDIO_BAD_SIZE;I9AudioStatus e=lock();if(e)return e;e=property_size(object,a,out);unlock();return e;}
I9AudioStatus PhysicalDevicePlugIn_ObjectGetPropertyData(I9AudioObjectID object,const struct i9_audio_address *a,
 uint32_t qualifier_size,const void *qualifier,uint32_t *bytes,void *data)
{
 (void)qualifier_size;(void)qualifier;if(!bytes)return I9_AUDIO_BAD_SIZE;
 I9AudioStatus e=lock();if(e)return e;uint32_t size=0;e=property_size(object,a,&size);
 if(e){unlock();return e;}if(*bytes<size||(size&&!data)){unlock();return I9_AUDIO_BAD_SIZE;}
 uint32_t word=0;CFRef string=0;double rate=(double)g.status.sample_rate;
 switch(a->selector){
 case FCC('l','n','a','m'):string=g.name;break;
 case FCC('l','m','a','k'):string=g.maker;break;
 case FCC('u','i','d',' '):case FCC('m','u','i','d'):string=g.uid;break;
 case FCC('n','a','m','e'):memcpy(data,"I9100 PCM",9);break;
 case FCC('a','s','r','t'):case FCC('n','s','r','t'):memcpy(data,&rate,8);break;
 case FCC('n','s','r','#'):{double values[2]={rate,rate};memcpy(data,values,16);break;}
 case FCC('s','f','m','t'):case FCC('p','f','t',' '):{struct i9_audio_format f=format();memcpy(data,&f,40);break;}
 case FCC('s','f','m','a'):case FCC('p','f','t','a'):{struct i9_audio_ranged_format f={format(),rate,rate};memcpy(data,&f,56);break;}
 case FCC('s','l','a','y'):{uint32_t layout[4]={a->scope==I9_AUDIO_INPUT?0U:1U,2,0,0};memcpy(data,layout,16);break;}
 case FCC('s','t','m','#'):case FCC('o','w','n','d'):if(size)memcpy(data,&g.stream,4);break;
 case FCC('d','c','h','2'):{uint32_t channels[2]={0,1};memcpy(data,channels,8);break;}
 case FCC('c','l','a','s'):word=object==g.device?FCC('a','d','e','v'):FCC('a','s','t','r');break;
 case FCC('b','c','l','s'):word=FCC('a','o','b','j');break;
 case FCC('p','l','u','g'):word=g.host->plugin_id;break;
 case FCC('g','o','i','n'):case FCC('g','o','n','e'):word=g.running;break;
 case FCC('l','i','v','n'):word=g.connection&&!g.fault&&!g.closing;break;
 case FCC('f','s','i','z'):word=g.status.period_frames;break;
 case FCC('t','e','r','m'):word=FCC('s','p','k','r');break;
 case FCC('t','r','a','n'):word=FCC('b','l','t','n');break;
 case FCC('s','a','c','t'):word=1;break;
 case FCC('d','f','l','t'):word=a->scope==I9_AUDIO_INPUT?0U:1U;break;
 case FCC('m','u','t','e'):word=(g.status.flags&I9100_AU_OUTPUT_ACTIVE)==0;break;
 case FCC('o','i','n','k'):word=0xffffffffU;break;
 default:break;
 }
 if(string){string=CFRetain(string);memcpy(data,&string,sizeof(string));}
 else if(size==4&&a->selector!=FCC('s','t','m','#')&&a->selector!=FCC('o','w','n','d'))memcpy(data,&word,4);
 *bytes=size;unlock();return 0;
}
I9AudioStatus PhysicalDevicePlugIn_ObjectSetPropertyData(I9AudioObjectID object,const struct i9_audio_address *a,
 uint32_t qualifier_size,const void *qualifier,uint32_t bytes,const void *data)
{
 (void)qualifier_size;(void)qualifier;I9AudioStatus e=lock();if(e)return e;
 if(!valid_object(object,a)||object!=g.device){unlock();return I9_AUDIO_BAD_OBJECT;}
 if(g.closing||!g.initialized||g.buffer_owned){unlock();return I9_AUDIO_UNSUPPORTED;}
 if(a->selector==FCC('n','s','r','t')||a->selector==FCC('a','s','r','t')){
  double value=0;if(bytes!=8||!data)e=I9_AUDIO_BAD_SIZE;else {memcpy(&value,data,8);e=value==(double)g.status.sample_rate?0:I9_AUDIO_UNSUPPORTED;}
  unlock();return e;
 }
 if(a->selector!=FCC('g','o','i','n')){unlock();return I9_AUDIO_UNSUPPORTED;}
 uint32_t running=2;if(bytes!=4||!data){unlock();return I9_AUDIO_BAD_SIZE;}memcpy(&running,data,4);
 if(running>1){unlock();return I9_AUDIO_UNSUPPORTED;}
 uint32_t changed=0;
 if(running&&!g.running){
  e=exchange(I9100_AU_PREPARE,g.status.sample_rate,g.status.period_frames,0,0);
  if(!e&&(g.status.flags&I9100_AU_LOW_OUTPUT_ALLOWED))e=exchange(I9100_AU_OUTPUT_PREPARE,2,(uint32_t)-30000,0,0);
  if(!e)e=exchange(I9100_AU_START,0,0,0,0);g.activation_attempted=0;changed=e?0U:1U;
 }else if(!running&&g.running){e=exchange(I9100_AU_STOP,0,0,0,0);changed=e?0U:1U;}
 struct i9_audio_host *host=g.host;I9AudioObjectID device=g.device;
 uint32_t notify=!e&&changed&&host->properties_changed;
 if(notify)g.callbacks++;unlock();
 if(notify){struct i9_audio_address address={FCC('g','o','i','n'),I9_AUDIO_GLOBAL,0};
  e=host->properties_changed(host,device,1,&address);
  if(lock())return I9_AUDIO_ERROR;g.callbacks--;unlock();}
 return e;
}
I9AudioStatus PhysicalDevicePlugIn_GetZeroTimeStamp(I9AudioObjectID device,double *sample,uint64_t *host,uint64_t *seed)
{
 if(!sample||!host||!seed)return I9_AUDIO_BAD_SIZE;I9AudioStatus e=lock();if(e)return e;
 if(device!=g.device||!g.initialized||g.closing||!g.running){unlock();return I9_AUDIO_UNSUPPORTED;}
 e=exchange(I9100_AU_STATUS,0,0,0,0);if(e){unlock();return e;}
 uint64_t frames=i9100_audio_user_u64(g.status.completed_frames_lo,g.status.completed_frames_hi);
 uint64_t ns=frames?i9100_audio_user_u64(g.status.completion_ns_lo,g.status.completion_ns_hi):i9100_audio_user_u64(g.status.start_ns_lo,g.status.start_ns_hi);
 if(!(g.status.flags&I9100_AU_TIMESTAMP_VALID)||!g.status.stream_seed||frames>9007199254740992ULL||
    ns/g.timebase.numer>UINT64_MAX/g.timebase.denom){unlock();return I9_AUDIO_UNSUPPORTED;}
 uint64_t quotient=ns/g.timebase.numer*g.timebase.denom;
 uint64_t tail=(ns%g.timebase.numer)*(uint64_t)g.timebase.denom/g.timebase.numer;
 if(quotient>UINT64_MAX-tail){unlock();return I9_AUDIO_UNSUPPORTED;}
 *sample=(double)frames;*host=quotient+tail;*seed=g.status.stream_seed;unlock();return 0;
}
I9AudioStatus PhysicalDevicePlugIn_BeginIOCycle(I9AudioObjectID device,uint32_t token,uint32_t frames,uint32_t flags,double begin,double end)
{(void)token;(void)flags;(void)begin;(void)end;I9AudioStatus e=lock();if(e)return e;
 e=device!=g.device||!g.initialized||g.closing||!g.running||!frames||frames>I9100_AUDIO_USER_MAX_FRAMES?I9_AUDIO_UNSUPPORTED:0;unlock();return e;}
I9AudioStatus PhysicalDevicePlugIn_EndIOCycle(I9AudioObjectID device,uint32_t token,uint32_t frames,uint32_t flags,double begin,double end)
{return PhysicalDevicePlugIn_BeginIOCycle(device,token,frames,flags,begin,end);}
I9AudioStatus PhysicalDevicePlugIn_AcquireIOBufferForStream(I9AudioObjectID device,I9AudioObjectID stream,uint32_t frames,
 uint32_t flags,double begin,double end,void **out)
{
 (void)flags;(void)begin;(void)end;if(!out)return I9_AUDIO_BAD_SIZE;*out=0;I9AudioStatus e=lock();if(e)return e;
 if(device!=g.device||stream!=g.stream||!g.initialized||g.closing||g.fault||!g.running||g.buffer_owned){unlock();return I9_AUDIO_UNSUPPORTED;}
 if(!frames||frames>I9100_AUDIO_USER_MAX_FRAMES){unlock();return I9_AUDIO_BAD_SIZE;}
 memset(g.buffer,0,frames*4U);g.buffer_owned=1;g.buffer_frames=frames;g.buffer_submitted=0;*out=g.buffer;unlock();return 0;
}
I9AudioStatus PhysicalDevicePlugIn_ReleaseIOBufferForStream(I9AudioObjectID device,I9AudioObjectID stream,uint32_t frames,
 uint32_t flags,double begin,double end,void *buffer)
{
 (void)frames;(void)flags;(void)begin;(void)end;I9AudioStatus e=lock();if(e)return e;
 if(device!=g.device||stream!=g.stream||buffer!=g.buffer||!g.buffer_owned){unlock();return I9_AUDIO_BAD_OBJECT;}
 memset(g.buffer,0,g.buffer_frames*4U);g.buffer_owned=g.buffer_frames=g.buffer_submitted=0;unlock();return 0;
}
I9AudioStatus PhysicalDevicePlugIn_ReadFromStream(I9AudioObjectID d,I9AudioObjectID s,uint32_t n,uint32_t f,double b,double e,void *p)
{(void)d;(void)s;(void)n;(void)f;(void)b;(void)e;(void)p;return I9_AUDIO_UNSUPPORTED;}
I9AudioStatus PhysicalDevicePlugIn_WriteToStream(I9AudioObjectID device,I9AudioObjectID stream,uint32_t frames,
 uint32_t flags,double begin,double end,void *buffer)
{
 (void)flags;(void)begin;(void)end;I9AudioStatus e=lock();if(e)return e;
 if(device!=g.device||stream!=g.stream||buffer!=g.buffer||!g.buffer_owned||g.buffer_submitted||
    !g.initialized||g.closing||g.fault||!g.running){unlock();return I9_AUDIO_UNSUPPORTED;}
 if(!frames||frames>g.buffer_frames){unlock();return I9_AUDIO_BAD_SIZE;}
 g.buffer_submitted=1; /* Even an ambiguous RPC failure must not replay this lease. */
 e=exchange(I9100_AU_ENQUEUE,0,0,(const int16_t *)buffer,frames);
 if(!e&&frames%g.status.period_frames)e=exchange(I9100_AU_FLUSH,0,0,0,0);
 if(!e&&!g.activation_attempted&&(g.status.flags&I9100_AU_LOW_OUTPUT_ALLOWED)&&
    i9100_audio_user_u64(g.status.silence_periods_lo,g.status.silence_periods_hi)){
  g.activation_attempted=1;e=exchange(I9100_AU_ACTIVATE,0,0,0,0);
 }
 unlock();return e;
}
