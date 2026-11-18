/* SPDX-License-Identifier: BSD-2-Clause; actual IOUserClient in gated mode. */
#define I9100_GATED_PIPELINE_ENTRY unused_gated_pipeline_reference
#include "i9100_audio_gated_pipeline_test.cpp"
#include "IOS6I9100AudioPCM.h"
#include "offline-native-sdk/IOKit/IOUserClient.h"
void IOLockLock(IOLock *lock){assert(!lock->held);lock->held=true;}
static struct {i9100_audio_user_request q;int16_t samples[1024];} user_packet;
static uint32_t user_session,user_seq;
static i9100_audio_user_reply call(IOUserClient *client,uint32_t op,uint32_t a=0,uint32_t b=0,uint32_t frames=0)
{
 user_packet.q={1,op,op?user_session:0,op?user_seq++:0,frames,a,b,0};
 i9100_audio_user_reply reply={};IOExternalMethodArguments args;
 args.structureInput=&user_packet;args.structureInputSize=32+frames*4U;
 args.structureOutput=&reply;args.structureOutputSize=128;
 assert(!client->externalMethod(0,&args,nullptr,nullptr,nullptr));
 if(!op){user_session=reply.session;user_seq=1;}return reply;
}
#ifndef I9100_GATED_USER_ENTRY
#define I9100_GATED_USER_ENTRY main
#endif
int I9100_GATED_USER_ENTRY()
{
 IOService platform;i9100_audio_board_native *board=nullptr;IOS6I9100AudioPCM *device=nullptr;
 gated_board_setup(&board);i9100_audio_user_config config={48000,64,1536000,250,0,0,1};
 assert(!i9100_audio_user_device_create(1,&platform,board,nullptr,&config,&device));
 IOService *service=reinterpret_cast<IOService *>(device);IOUserClient *client=nullptr;
 assert(!service->newUserClient((task_t)1,nullptr,0,nullptr,&client));
 auto reply=call(client,I9100_AU_INFO);assert(!reply.error&&(reply.flags&I9100_AU_GATED_DMA));
 reply=call(client,I9100_AU_PREPARE,48000,64);assert(!reply.error);m.extra_contexts=1;
 reply=call(client,I9100_AU_OUTPUT_PREPARE,2,(uint32_t)-30000);assert(!reply.error);
 reply=call(client,I9100_AU_START);assert(!reply.error&&vm_state.words==64);
 m.ns+=1000000ULL;last_loop->tick();reply=call(client,I9100_AU_STATUS);
 assert(!reply.error&&reply.completed_frames_lo==64&&reply.silence_periods_lo==1&&reply.stream_seed==1);
 reply=call(client,I9100_AU_ACTIVATE);assert(!reply.error&&(reply.flags&I9100_AU_OUTPUT_ACTIVE));
 for(unsigned n=0;n<128;n++)user_packet.samples[n]=(int16_t)(n*291U);
 for(unsigned turn=0;turn<32;turn++){
  reply=call(client,I9100_AU_ENQUEUE,0,0,64);assert(!reply.error&&reply.accepted_frames==64);
  m.ns+=1000000ULL;last_loop->tick();reply=call(client,I9100_AU_STATUS);
  assert(!reply.error&&reply.completed_periods_lo==turn+2U&&(reply.flags&I9100_AU_GATED_DMA));
 }
 assert((m.codec[4][89]&127U)==66U&&(m.codec[2][37]&31U)==1U);
 reply=call(client,I9100_AU_STOP);assert(!reply.error&&!(reply.flags&I9100_AU_RUNNING)&&!m.dma_count);
 assert(!client->clientClose());client->stop(service);client->detach(service);client->release();
 assert(!i9100_audio_user_device_destroy(&device)&&!device&&!event_objects);m.extra_contexts=0;
 assert(!i9100_audio_board_close(&board));board_released();
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 puts("PASS actual gated IOUserClient -> workloop -> codec/I2S -> DMA VM: mode flag, exact sequence/counts/zero pre-roll/low gain/stop");return 0;
}
