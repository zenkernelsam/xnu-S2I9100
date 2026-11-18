/* SPDX-License-Identifier: BSD-2-Clause; real user client -> worker -> PCM/codec. */
#define I9100_WORKER_MODEL_ENTRY original_worker_suite_main
#include "i9100_audio_worker_test.cpp"
#include "IOS6I9100AudioPCM.h"
#include "offline-native-sdk/IOKit/IOUserClient.h"
void IOLockLock(IOLock *lock){assert(!lock->held);lock->held=true;}
static struct {i9100_audio_user_request q;int16_t samples[1024];} packet;
static uint32_t session_id,sequence;
static IOReturn method(IOUserClient *client,i9100_audio_user_reply *reply,uint32_t bytes=32)
{
 IOExternalMethodArguments a;a.structureInput=&packet;a.structureInputSize=bytes;
 a.structureOutput=reply;a.structureOutputSize=sizeof(*reply);
 return client->externalMethod(0,&a,nullptr,nullptr,nullptr);
}
static i9100_audio_user_reply command(IOUserClient *c,uint32_t op,uint32_t a=0,uint32_t b=0,uint32_t frames=0)
{
 packet.q={1,op,op?session_id:0,op?sequence++:0,frames,a,b,0};i9100_audio_user_reply reply={};
 assert(method(c,&reply,32+frames*4U)==0);if(!op){session_id=reply.session;sequence=1;}return reply;
}
static void close_client(IOUserClient *c,IOService *parent)
{c->stop(parent);c->detach(parent);c->release();}
static void end_device(IOS6I9100AudioPCM **d,i9100_audio_board_native **b)
{assert(!i9100_audio_user_device_destroy(d)&&!*d&&!event_objects);m.extra_contexts=0;assert(!i9100_audio_board_close(b));board_released();}
#ifndef I9100_USER_MODEL_ENTRY
#define I9100_USER_MODEL_ENTRY main
#endif
int I9100_USER_MODEL_ENTRY()
{
 assert(!original_worker_suite_main());
 IOService platform;i9100_audio_board_native *board=nullptr;i9100_irq_registry registry;IOS6I9100AudioPCM *d=nullptr;
 i9100_audio_user_config config={48000,64,1536000,250,0,0,0};
 assert(i9100_audio_user_device_create(0,nullptr,nullptr,nullptr,nullptr,&d)==I9100_AU_DISABLED&&!d);
 setup_worker(&board,&registry);m.extra_contexts=0;
 assert(!i9100_audio_user_device_create(1,&platform,board,&registry,&config,&d));
 auto *service=reinterpret_cast<IOService *>(d);IOUserClient *client=nullptr,*other=nullptr;
 assert(!service->newUserClient((task_t)1,nullptr,0,nullptr,&client));
 assert(service->newUserClient((task_t)2,nullptr,0,nullptr,&other)==kIOReturnExclusiveAccess&&!other);
 auto info=command(client,I9100_AU_INFO);assert(info.sample_rate==48000&&info.period_frames==64&&!(info.flags&I9100_AU_LOW_OUTPUT_ALLOWED));
 auto reply=command(client,I9100_AU_PREPARE,48000,64);assert(!reply.error&&(reply.flags&I9100_AU_READY));m.extra_contexts=1;
 unsigned before=m.transactions;reply=command(client,I9100_AU_OUTPUT_PREPARE,2,(uint32_t)-30000);
 assert(reply.error==I9100_AU_OUTPUT_DENIED&&m.transactions==before);
 packet.samples[0]=32767;packet.samples[1]=-32768;reply=command(client,I9100_AU_ENQUEUE,0,0,1);
 assert(!reply.error&&reply.accepted_frames==1);reply=command(client,I9100_AU_FLUSH);assert(!reply.error);
 reply=command(client,I9100_AU_START);assert(!reply.error&&(reply.flags&I9100_AU_RUNNING));
 assert(((int16_t *)allocation_bytes[0])[0]==1035&&((int16_t *)allocation_bytes[0])[1]==-1036);
 unsigned writes=m.transactions;packet.q.sequence--;
 assert(method(client,&reply)==kIOReturnBadArgument&&m.transactions==writes);
 m.ns+=2000000;m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();
 reply=command(client,I9100_AU_STATUS);assert(!reply.error&&reply.completed_frames_lo==64&&(reply.flags&I9100_AU_TIMESTAMP_VALID));
 uint64_t first_time=i9100_audio_user_u64(reply.completion_ns_lo,reply.completion_ns_hi);
 last_loop->tick();reply=command(client,I9100_AU_STATUS);assert(reply.completed_frames_lo==64&&i9100_audio_user_u64(reply.completion_ns_lo,reply.completion_ns_hi)==first_time);
 reply=command(client,I9100_AU_STOP);assert(!reply.error&&(reply.flags&I9100_AU_CLOSED));m.extra_contexts=0;
 reply=command(client,I9100_AU_PREPARE,48000,64);assert(!reply.error&&reply.stream_seed==2);m.extra_contexts=1;
 assert(!client->clientClose());close_client(client,service);end_device(&d,&board);
 /* Inline scalar/descriptor/oversize/async methods cannot reach hardware. */
 setup_worker(&board,&registry);m.extra_contexts=0;assert(!i9100_audio_user_device_create(1,&platform,board,&registry,&config,&d));
 service=reinterpret_cast<IOService *>(d);assert(!service->newUserClient((task_t)1,nullptr,0,nullptr,&client));command(client,I9100_AU_INFO);
 IOExternalMethodArguments bad;bad.structureInput=&packet;bad.structureInputSize=32;bad.structureOutput=&reply;bad.structureOutputSize=128;
 before=m.transactions;bad.scalarInputCount=1;assert(client->externalMethod(0,&bad,nullptr,nullptr,nullptr)==kIOReturnBadArgument);
 bad.scalarInputCount=0;bad.structureInputDescriptor=(IOMemoryDescriptor *)1;assert(client->externalMethod(0,&bad,nullptr,nullptr,nullptr)==kIOReturnBadArgument);
 bad.structureInputDescriptor=nullptr;bad.asyncWakePort=1;assert(client->externalMethod(0,&bad,nullptr,nullptr,nullptr)==kIOReturnBadArgument);
 assert(m.transactions==before);assert(i9100_audio_user_device_destroy(&d)==I9100_PCM_BUSY&&d);
 reply=command(client,I9100_AU_PREPARE,48000,64);assert(reply.error==I9100_AU_DISABLED);
 assert(!client->clientClose());close_client(client,service);end_device(&d,&board);
 /* Actual zero completion gates the explicit low-output activation. */
 config.approved_low_output=1;setup_worker(&board,&registry);m.extra_contexts=0;
 assert(!i9100_audio_user_device_create(1,&platform,board,&registry,&config,&d));service=reinterpret_cast<IOService *>(d);
 assert(!service->newUserClient((task_t)1,nullptr,0,nullptr,&client));command(client,I9100_AU_INFO);
 reply=command(client,I9100_AU_PREPARE,48000,64);assert(!reply.error);m.extra_contexts=1;
 reply=command(client,I9100_AU_OUTPUT_PREPARE,2,(uint32_t)-30000);assert(!reply.error&&reply.output_route==2);
 reply=command(client,I9100_AU_ENQUEUE,0,0,64);assert(!reply.error);reply=command(client,I9100_AU_START);assert(!reply.error);
 reply=command(client,I9100_AU_ACTIVATE);assert(reply.error==I9100_MC_NOT_READY);
 m.ns+=2000000;m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;last_loop->tick();m.pdma[0x100/4]=1;
 reply=command(client,I9100_AU_ACTIVATE);assert(!reply.error&&(reply.flags&I9100_AU_OUTPUT_ACTIVE));
 assert((m.codec[4][89]&0x7fU)==66U&&(m.codec[2][37]&0x1fU)==1U);
 /* Closing the user port during a sticky mute failure must keep resources. */
 m.mute_fail=1;assert(client->clientDied()==kIOReturnIOError);
 assert(i9100_audio_board_close(&board)==I9100_AP_BUSY&&board&&m.dma_count);
 assert(service->newUserClient((task_t)2,nullptr,0,nullptr,&other)==kIOReturnNotReady&&!other);
 close_client(client,service);assert(i9100_audio_user_device_destroy(&d)!=0&&d);
 m.mute_fail=0;m.pdma[0x100/4]=0;end_device(&d,&board);
 /* Restore host BSS pages before LeakSanitizer scans global roots. The
  * powered-off PROT_NONE model is only meaningful while the test is live. */
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 puts("PASS real user-client boundary -> workloop -> owned native PCM/codec; zero pre-roll, low gain, timestamps, fresh session, death/fault retention");return 0;
}
