/* SPDX-License-Identifier: BSD-2-Clause */
#include "IOS6I9100AudioPCM.h"
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLocks.h>
#include <string.h>
class IOS6I9100AudioPCMClient;
class IOS6I9100AudioPCM : public IOService {
 OSDeclareDefaultStructors(IOS6I9100AudioPCM)
 struct {
  i9100_audio_board_native *board;i9100_irq_registry *registry;i9100_audio_worker *worker;
  IOLock *lock;IOS6I9100AudioPCMClient *owner;i9100_audio_user_config config;
  uint32_t generation,stream_seed,closing,quarantined,output_route;int cleanup_error;
  i9100_audio_user_reply last;
 } s;
 static int invoke(void *,uint32_t,uint32_t,uint32_t,const int16_t *,uint32_t);
 static int snapshot(void *,i9100_audio_user_reply *);
 int makeWorker();int stopWorker();
public:
 virtual bool init();virtual void free();virtual bool start(IOService *);
 virtual void stop(IOService *);
 virtual IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **);
 int configure(i9100_audio_board_native *,i9100_irq_registry *,const i9100_audio_user_config *);
 int exchange(IOS6I9100AudioPCMClient *,i9100_audio_user_session *,const void *,size_t,i9100_audio_user_reply *);
 int closeClient(IOS6I9100AudioPCMClient *,i9100_audio_user_session *);int shutdown();
};
class IOS6I9100AudioPCMClient : public IOUserClient {
 OSDeclareDefaultStructors(IOS6I9100AudioPCMClient)
 IOS6I9100AudioPCM *device;i9100_audio_user_session session;
public:
 bool initialize(task_t task,void *security,UInt32 type,OSDictionary *properties){
  device=0;memset(&session,0,sizeof(session));return IOUserClient::initWithTask(task,security,type,properties);
 }
 int configure(IOS6I9100AudioPCM *d,const i9100_audio_user_io *io,uint32_t generation,int output){
  int e=i9100_audio_user_session_init(&session,1,io,generation,output);if(!e){device=d;d->retain();}return e;
 }
 virtual IOReturn clientClose();virtual IOReturn clientDied(){return clientClose();}
 virtual void free(){auto *d=device;if(d)d->closeClient(this,&session);device=0;IOUserClient::free();if(d)d->release();}
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *);
};
OSDefineMetaClassAndStructors(IOS6I9100AudioPCM,IOService)
OSDefineMetaClassAndStructors(IOS6I9100AudioPCMClient,IOUserClient)
bool IOS6I9100AudioPCM::init()
{memset(&s,0,sizeof(s));if(!IOService::init())return false;s.lock=IOLockAlloc();return s.lock!=0;}
void IOS6I9100AudioPCM::free()
{
 /* Factory/destroy retain the original creation reference until all actual
  * child resources are gone. A caller must never release a failed destroy. */
 if(s.lock)IOLockFree(s.lock);IOService::free();
}
int IOS6I9100AudioPCM::makeWorker()
{
 if(s.worker)return 0;
 int e=i9100_audio_worker_create(1,s.board,s.registry,s.config.use_irq,
  s.config.verified_dispatch_hook,s.config.poll_us,&s.worker);
 if(e){s.quarantined=1;s.cleanup_error=e;}return e;
}
int IOS6I9100AudioPCM::configure(i9100_audio_board_native *board,i9100_irq_registry *registry,
 const i9100_audio_user_config *config)
{s.board=board;s.registry=registry;s.config=*config;return makeWorker();}
bool IOS6I9100AudioPCM::start(IOService *provider)
{
 if(!s.worker||s.closing||s.quarantined||!IOService::start(provider))return false;
 setName(I9100_AUDIO_USER_CLASS);setProperty("I9100AudioABI",I9100_AUDIO_USER_ABI,32);
 setProperty("I9100AudioDigitalCapMilliDB",(uint64_t)(uint32_t)-30000,32);
 setProperty("I9100AudioAnalogCapMilliDB",(uint64_t)(uint32_t)-36000,32);
 setProperty("I9100AudioLowOutputAllowed",s.config.approved_low_output!=0);
 registerService();return true;
}
int IOS6I9100AudioPCM::snapshot(void *cookie,i9100_audio_user_reply *out)
{
 auto *d=(IOS6I9100AudioPCM *)cookie;i9100_audio_user_reply v={};
 v.sample_rate=d->s.config.sample_rate;v.period_frames=d->s.config.period_frames;
 v.stream_seed=d->s.stream_seed;v.output_route=d->s.output_route;
 if(!d->s.worker){v=d->s.last;v.flags&=~(I9100_AU_READY|I9100_AU_RUNNING|I9100_AU_OUTPUT_ACTIVE);
  v.flags|=I9100_AU_CLOSED;*out=v;return 0;}
 struct i9100_audio_worker_status w={};int e=i9100_audio_worker_status(d->s.worker,&w);if(e)return e;
 if(w.running)v.flags|=I9100_AU_RUNNING;
 if(w.fault||d->s.quarantined)v.flags|=I9100_AU_FAULT;
 if(w.closed||d->s.closing)v.flags|=I9100_AU_CLOSED;
 if(w.output_prepared)v.flags|=I9100_AU_OUTPUT_PREPARED;
 if(w.output_active)v.flags|=I9100_AU_OUTPUT_ACTIVE;
 v.cleanup_error=w.cleanup_error?w.cleanup_error:d->s.cleanup_error;v.last_error=w.error;
 v.staged_frames=w.staged_frames;v.capacity_frames=w.staging_capacity;
 if(w.last_pcm_valid){const auto &pcm=w.last_pcm;
  if(pcm.prepared&&!w.fault&&!w.closed&&!d->s.closing)v.flags|=I9100_AU_READY;
  if(pcm.timestamp_valid)v.flags|=I9100_AU_TIMESTAMP_VALID;
  v.completed_frames_lo=(uint32_t)pcm.completed_frames;v.completed_frames_hi=(uint32_t)(pcm.completed_frames>>32);
  v.completed_periods_lo=(uint32_t)pcm.completed_periods;v.completed_periods_hi=(uint32_t)(pcm.completed_periods>>32);
  v.silence_periods_lo=(uint32_t)pcm.completed_silence_periods;v.silence_periods_hi=(uint32_t)(pcm.completed_silence_periods>>32);
  v.start_ns_lo=(uint32_t)pcm.start_time_ns;v.start_ns_hi=(uint32_t)(pcm.start_time_ns>>32);
  v.completion_ns_lo=(uint32_t)pcm.completion_time_ns;v.completion_ns_hi=(uint32_t)(pcm.completion_time_ns>>32);
  v.fifo_errors=pcm.fifo_errors;
 }
 if(w.last_board_valid){v.board_phase=w.last_board.phase;v.board_stage=w.last_board.stage;
  v.output_gain_mdb=w.last_board.output_gain_mdb;}
 d->s.last=v;*out=v;return 0;
}
int IOS6I9100AudioPCM::stopWorker()
{
 if(!s.worker)return 0;
 int e=i9100_audio_worker_stop(s.worker);i9100_audio_user_reply status={};
 int se=snapshot(this,&status);if(!e&&se)e=se;
 if(!e)e=i9100_audio_worker_destroy(&s.worker);
 if(e){s.quarantined=1;s.cleanup_error=e;}return e;
}
int IOS6I9100AudioPCM::invoke(void *cookie,uint32_t op,uint32_t arg0,uint32_t arg1,
 const int16_t *samples,uint32_t frames)
{
 auto *d=(IOS6I9100AudioPCM *)cookie;
 if(op==I9100_AU_STATUS)return 0;
 if(op==I9100_AU_STOP)return d->stopWorker();
 if(d->s.closing||d->s.quarantined)return I9100_AU_DISABLED;
 if(op==I9100_AU_PREPARE){
  if(arg0!=d->s.config.sample_rate||arg1!=d->s.config.period_frames||d->s.stream_seed==0xffffffffU)
   return I9100_AU_ARGUMENT;
  int e=d->makeWorker();if(e)return e;
  e=i9100_audio_worker_prepare(d->s.worker,d->s.config.measured_bclk);if(e)return e;
  struct i9100_audio_worker_status w={};e=i9100_audio_worker_status(d->s.worker,&w);
  if(!e&&(!w.last_pcm_valid||w.last_pcm.sample_rate!=arg0||w.last_pcm.period_frames!=arg1))e=I9100_AU_ARGUMENT;
  if(e){d->s.quarantined=1;int cleanup=d->stopWorker();if(cleanup)d->s.cleanup_error=cleanup;return e;}
  d->s.stream_seed++;return 0;
 }
 if(!d->s.worker)return I9100_AU_DISABLED;
 switch(op){
 case I9100_AU_ENQUEUE:return i9100_audio_worker_enqueue(d->s.worker,samples,frames);
 case I9100_AU_START:return i9100_audio_worker_start(d->s.worker);
 case I9100_AU_OUTPUT_PREPARE:{int e=i9100_audio_worker_output_prepare(1,d->s.worker,
  (i9100_mc1n2_output)arg0,(int32_t)arg1);if(!e)d->s.output_route=arg0;return e;}
 case I9100_AU_ACTIVATE:return i9100_audio_worker_output_activate(d->s.worker);
 case I9100_AU_MUTE:return i9100_audio_worker_output_mute(d->s.worker);
 case I9100_AU_FLUSH:return i9100_audio_worker_flush(d->s.worker);
 default:return I9100_AU_ARGUMENT;
 }
}
int IOS6I9100AudioPCM::exchange(IOS6I9100AudioPCMClient *client,i9100_audio_user_session *session,
 const void *input,size_t bytes,i9100_audio_user_reply *out)
{
 IOLockLock(s.lock);int e=s.owner!=client?I9100_AU_OWNER:i9100_audio_user_exchange(session,input,bytes,out);
 IOLockUnlock(s.lock);return e;
}
int IOS6I9100AudioPCM::closeClient(IOS6I9100AudioPCMClient *client,i9100_audio_user_session *session)
{
 IOLockLock(s.lock);int e=s.owner==client?stopWorker():0;
 if(s.owner==client)s.owner=0;if(session)session->enabled=0;IOLockUnlock(s.lock);return e;
}
int IOS6I9100AudioPCM::shutdown()
{
 IOLockLock(s.lock);s.closing=1;int e=s.owner?I9100_PCM_BUSY:stopWorker();IOLockUnlock(s.lock);return e;
}
void IOS6I9100AudioPCM::stop(IOService *provider)
{(void)shutdown();IOService::stop(provider);}
IOReturn IOS6I9100AudioPCM::newUserClient(task_t task,void *security,UInt32 type,
 OSDictionary *properties,IOUserClient **out)
{
 if(!task||!out||type)return kIOReturnBadArgument;*out=0;
 auto *client=new IOS6I9100AudioPCMClient;if(!client)return kIOReturnNoMemory;
 if(!client->initialize(task,security,type,properties)){client->release();return kIOReturnError;}
 if(!client->attach(this)){client->release();return kIOReturnError;}
 if(!client->start(this)){client->detach(this);client->release();return kIOReturnError;}
 IOLockLock(s.lock);int e=0;
 if(s.owner)e=I9100_PCM_BUSY;
 else if(s.closing||s.quarantined||s.generation==0xffffffffU)e=I9100_AU_DISABLED;
 else {e=makeWorker();if(!e){i9100_audio_user_io io={this,invoke,snapshot};
   e=client->configure(this,&io,++s.generation,s.config.approved_low_output);if(!e)s.owner=client;}}
 IOLockUnlock(s.lock);
 if(e){client->stop(this);client->detach(this);client->release();return e==I9100_PCM_BUSY?kIOReturnExclusiveAccess:kIOReturnNotReady;}
 *out=client;return kIOReturnSuccess;
}
IOReturn IOS6I9100AudioPCMClient::clientClose()
{
 int e=device?device->closeClient(this,&session):0;
 terminate();return e?kIOReturnIOError:kIOReturnSuccess;
}
IOReturn IOS6I9100AudioPCMClient::externalMethod(uint32_t selector,IOExternalMethodArguments *a,
 IOExternalMethodDispatch *,OSObject *,void *)
{
 if(selector)return kIOReturnUnsupported;
 if(!a||a->version!=kIOExternalMethodArgumentsCurrentVersion||a->asyncWakePort||a->asyncReferenceCount||
    a->scalarInputCount||a->scalarOutputCount||a->structureInputDescriptor||a->structureOutputDescriptor||
    !a->structureInput||!a->structureOutput||a->structureInputSize<sizeof(i9100_audio_user_request)||
    a->structureInputSize>sizeof(i9100_audio_user_request)+I9100_AUDIO_USER_MAX_FRAMES*4U||
    a->structureOutputSize<sizeof(i9100_audio_user_reply)||a->structureVariableOutputData)return kIOReturnBadArgument;
 if(!device)return kIOReturnNotAttached;
 i9100_audio_user_reply reply={};int e=device->exchange(this,&session,a->structureInput,a->structureInputSize,&reply);
 if(e)return e==I9100_AU_OWNER?kIOReturnNotPermitted:kIOReturnBadArgument;
 memcpy(a->structureOutput,&reply,sizeof(reply));a->structureOutputSize=sizeof(reply);return kIOReturnSuccess;
}
extern "C" int i9100_audio_user_device_create(int enabled,IOService *platform,
 i9100_audio_board_native *board,i9100_irq_registry *registry,const i9100_audio_user_config *config,
 IOS6I9100AudioPCM **out)
{
 if(!out||*out||(enabled!=0&&enabled!=1))return I9100_AU_ARGUMENT;
 if(!enabled)return I9100_AU_DISABLED;
 if(!platform||!board||!config||(config->sample_rate!=44100U&&config->sample_rate!=48000U)||
    config->period_frames<16U||config->period_frames>I9100_AUDIO_USER_MAX_FRAMES||
    config->measured_bclk!=config->sample_rate*32U||config->poll_us<50U||config->poll_us>2000U||
    (config->approved_low_output!=0&&config->approved_low_output!=1)||
    (config->use_irq!=0&&config->use_irq!=1)||
    (config->use_irq&&(!registry||config->verified_dispatch_hook!=1)))return I9100_AU_ARGUMENT;
 auto *d=new IOS6I9100AudioPCM;if(!d)return I9100_PCM_IO;
 if(!d->init()){d->release();return I9100_PCM_IO;}
 *out=d;int e=d->configure(board,registry,config);
 if(!e&&(!d->attach(platform)||!d->start(platform)))e=I9100_PCM_IO;
 if(e){int cleanup=i9100_audio_user_device_destroy(out);return cleanup?cleanup:e;}return 0;
}
extern "C" int i9100_audio_user_device_destroy(IOS6I9100AudioPCM **pointer)
{
 if(!pointer)return I9100_AU_ARGUMENT;if(!*pointer)return 0;
 auto *d=*pointer;int e=d->shutdown();if(e)return e;
 if(!d->terminate())return I9100_PCM_BUSY;
 if(d->getProvider())d->detach(d->getProvider());d->release();*pointer=0;return 0;
}
