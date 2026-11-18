/* SPDX-License-Identifier: BSD-2-Clause; protocol and failure boundaries. */
#include "i9100_audio_user_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {unsigned invoked,snapshots;int error,snapshot_error;uint32_t frames;int16_t first,last;} m;
static int invoke(void *cookie,uint32_t op,uint32_t a,uint32_t b,const int16_t *data,uint32_t frames)
{
 assert(cookie==&m);(void)op;(void)a;(void)b;m.invoked++;m.frames=frames;
 if(frames){m.first=data[0];m.last=data[frames*2U-1U];}return m.error;
}
static int snapshot(void *cookie,struct i9100_audio_user_reply *out)
{assert(cookie==&m);m.snapshots++;memset(out,0xaa,sizeof(*out));out->sample_rate=48000;out->period_frames=64;return m.snapshot_error;}
int main(void)
{
 typedef char request_size[(sizeof(struct i9100_audio_user_request)==32)?1:-1];
 typedef char reply_size[(sizeof(struct i9100_audio_user_reply)==128)?1:-1];
 (void)sizeof(request_size);(void)sizeof(reply_size);
 struct i9100_audio_user_session s={};struct i9100_audio_user_io io={&m,invoke,snapshot};
 assert(i9100_audio_user_session_init(&s,0,0,0,0)==I9100_AU_DISABLED&&!s.initialized);
 assert(!i9100_audio_user_session_init(&s,1,&io,77,0));
 struct {struct i9100_audio_user_request q;int16_t samples[I9100_AUDIO_USER_MAX_FRAMES*2U];} packet={};
 struct i9100_audio_user_reply reply;packet.q.abi=1;
 assert(!i9100_audio_user_exchange(&s,&packet.q,32,&reply)&&!m.invoked&&reply.session==77&&reply.sequence==0);
 assert(!(reply.flags&I9100_AU_LOW_OUTPUT_ALLOWED)&&!reply.reserved&&reply.error==0&&reply.snapshot_error==0);
 assert(reply.max_frames==512&&reply.channels==2);
 unsigned snapshots=m.snapshots;
 for(unsigned n=0;n<32U;n++)assert(i9100_audio_user_exchange(&s,&packet.q,n,&reply)==I9100_AU_ARGUMENT);
 packet.q.op=I9100_AU_ENQUEUE;packet.q.session=77;packet.q.sequence=1;packet.q.frames=512;
 packet.samples[0]=32767;packet.samples[1023]=-32768;
 assert(!i9100_audio_user_exchange(&s,&packet,sizeof(packet),&reply)&&reply.accepted_frames==512&&s.next_sequence==2);
 assert(m.first==32767&&m.last==-32768&&m.invoked==1);snapshots=m.snapshots;
 assert(i9100_audio_user_exchange(&s,&packet,sizeof(packet),&reply)==I9100_AU_SEQUENCE&&m.snapshots==snapshots&&m.invoked==1);
 packet.q.sequence=2;packet.q.session=78;
 assert(i9100_audio_user_exchange(&s,&packet,sizeof(packet),&reply)==I9100_AU_OWNER);
 packet.q.session=77;
 for(unsigned field=0;field<5U;field++){
  struct i9100_audio_user_request bad=packet.q;
  if(field==0)bad.frames=513;if(field==1)bad.frames=0;if(field==2)bad.arg0=1;if(field==3)bad.arg1=1;if(field==4)bad.reserved=1;
  packet.q=bad;assert(i9100_audio_user_exchange(&s,&packet,sizeof(packet),&reply)==I9100_AU_ARGUMENT);
  packet.q=(struct i9100_audio_user_request){1,I9100_AU_ENQUEUE,77,2,512,0,0,0};
 }
 assert(m.invoked==1&&s.next_sequence==2);
 m.snapshot_error=-99;assert(!i9100_audio_user_exchange(&s,&packet,sizeof(packet),&reply));
 assert(reply.accepted_frames==512&&!reply.error&&reply.snapshot_error==-99&&reply.sample_rate==0&&s.next_sequence==3);
 assert(i9100_audio_user_exchange(&s,&packet,sizeof(packet),&reply)==I9100_AU_SEQUENCE);
 packet.q=(struct i9100_audio_user_request){1,I9100_AU_START,77,3,0,0,0,0};m.error=-25;m.snapshot_error=0;
 assert(!i9100_audio_user_exchange(&s,&packet,32,&reply)&&reply.error==-25&&s.next_sequence==4&&m.invoked==3);
 packet.q=(struct i9100_audio_user_request){1,I9100_AU_OUTPUT_PREPARE,77,4,0,2,(uint32_t)-30000,0};
 assert(!i9100_audio_user_exchange(&s,&packet,32,&reply)&&reply.error==I9100_AU_OUTPUT_DENIED&&m.invoked==3&&s.next_sequence==5);
 packet.q.sequence=5;packet.q.arg1=(uint32_t)-28000;
 assert(i9100_audio_user_exchange(&s,&packet,32,&reply)==I9100_AU_ARGUMENT&&s.next_sequence==5);
 struct i9100_audio_user_session copy=s;
 assert(i9100_audio_user_exchange(&copy,&packet,32,&reply)==I9100_AU_OWNER);
 packet.q=(struct i9100_audio_user_request){1,I9100_AU_STATUS,77,0xffffffffU,0,0,0,0};s.next_sequence=0xffffffffU;
 assert(i9100_audio_user_exchange(&s,&packet,32,&reply)==I9100_AU_SEQUENCE&&s.next_sequence==0xffffffffU);
 struct i9100_audio_user_session allowed={};assert(!i9100_audio_user_session_init(&allowed,1,&io,78,1));
 packet.q=(struct i9100_audio_user_request){1,I9100_AU_OUTPUT_PREPARE,78,1,0,1,(uint32_t)-36000,0};m.error=0;
 assert(!i9100_audio_user_exchange(&allowed,&packet,32,&reply)&&!reply.error&&(reply.flags&I9100_AU_LOW_OUTPUT_ALLOWED));
 puts("PASS audio wire: exact sizes/whole signed16 payload/generation/no replay/no wrap/accepted-vs-snapshot/immutable low-output permission");return 0;
}
