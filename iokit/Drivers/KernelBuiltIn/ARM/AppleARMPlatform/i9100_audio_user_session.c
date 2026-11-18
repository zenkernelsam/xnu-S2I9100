/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_audio_user_session.h"
#include <string.h>
int i9100_audio_user_session_init(struct i9100_audio_user_session *s,int enabled,
 const struct i9100_audio_user_io *io,uint32_t session,int output)
{
 if(!s||s->initialized||(enabled!=0&&enabled!=1)||(output!=0&&output!=1))return I9100_AU_ARGUMENT;
 if(!enabled)return I9100_AU_DISABLED;
 if(!io||!io->invoke||!io->snapshot||!session)return I9100_AU_ARGUMENT;
 memset(s,0,sizeof(*s));s->io=*io;s->self=(uintptr_t)s;s->session=session;
 s->next_sequence=1;s->enabled=s->initialized=1;s->low_output_allowed=(uint32_t)output;return 0;
}
int i9100_audio_user_exchange(struct i9100_audio_user_session *s,const void *input,size_t bytes,
 struct i9100_audio_user_reply *out)
{
 if(!s||!s->initialized||!input||!out||bytes<sizeof(struct i9100_audio_user_request))return I9100_AU_ARGUMENT;
 if(s->self!=(uintptr_t)s)return I9100_AU_OWNER;if(!s->enabled)return I9100_AU_DISABLED;
 struct i9100_audio_user_request q;memcpy(&q,input,sizeof(q));
 if(q.abi!=I9100_AUDIO_USER_ABI||q.reserved||q.op>I9100_AU_FLUSH)return I9100_AU_ARGUMENT;
 if(q.op==I9100_AU_ENQUEUE){
  if(!q.frames||q.frames>I9100_AUDIO_USER_MAX_FRAMES||q.arg0||q.arg1||
     bytes!=sizeof(q)+q.frames*4U||((uintptr_t)input&1U))return I9100_AU_ARGUMENT;
 }else if(q.frames||bytes!=sizeof(q))return I9100_AU_ARGUMENT;
 if(q.op==I9100_AU_OUTPUT_PREPARE){
  int32_t gain=(int32_t)q.arg1;
  if((q.arg0!=1U&&q.arg0!=2U)||gain>-30000||gain<-96000)return I9100_AU_ARGUMENT;
 }else if(q.op!=I9100_AU_PREPARE&&(q.arg0||q.arg1))return I9100_AU_ARGUMENT;
 if(q.op==I9100_AU_INFO){if(q.session||q.sequence)return I9100_AU_ARGUMENT;}
 else if(q.session!=s->session)return I9100_AU_OWNER;
 else if(q.sequence!=s->next_sequence||q.sequence>=0xffffffffU)return I9100_AU_SEQUENCE;
 int error=0;
 if(q.op!=I9100_AU_INFO){
  s->next_sequence++;
  if((q.op==I9100_AU_OUTPUT_PREPARE||q.op==I9100_AU_ACTIVATE)&&!s->low_output_allowed)
   error=I9100_AU_OUTPUT_DENIED;
  else error=s->io.invoke(s->io.cookie,q.op,q.arg0,q.arg1,
    q.op==I9100_AU_ENQUEUE?(const int16_t *)((const uint8_t *)input+sizeof(q)):0,q.frames);
 }
 struct i9100_audio_user_reply value;memset(&value,0,sizeof(value));
 int snapshot=s->io.snapshot(s->io.cookie,&value);
 if(snapshot)memset(&value,0,sizeof(value));
 value.abi=I9100_AUDIO_USER_ABI;value.session=s->session;value.sequence=q.sequence;
 value.error=error;value.snapshot_error=snapshot;
 value.max_frames=I9100_AUDIO_USER_MAX_FRAMES;value.channels=2;value.reserved=0;
 value.flags&=~I9100_AU_LOW_OUTPUT_ALLOWED;if(s->low_output_allowed)value.flags|=I9100_AU_LOW_OUTPUT_ALLOWED;
 value.accepted_frames=q.op==I9100_AU_ENQUEUE&&!error?q.frames:0;
 *out=value;return 0;
}
