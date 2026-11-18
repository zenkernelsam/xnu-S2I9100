/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_AUDIO_USER_SESSION_H
#define I9100_AUDIO_USER_SESSION_H
#include "i9100_audio_user.h"
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_audio_user_io {
 void *cookie;
 int (*invoke)(void *,uint32_t,uint32_t,uint32_t,const int16_t *,uint32_t);
 int (*snapshot)(void *,struct i9100_audio_user_reply *);
};
struct i9100_audio_user_session {
 struct i9100_audio_user_io io;
 uintptr_t self;
 uint32_t initialized,enabled,session,next_sequence,low_output_allowed;
};
int i9100_audio_user_session_init(struct i9100_audio_user_session *,int enabled,
 const struct i9100_audio_user_io *,uint32_t session,int low_output_allowed);
int i9100_audio_user_exchange(struct i9100_audio_user_session *,const void *,size_t,
 struct i9100_audio_user_reply *);
/* Owner serializes this struct and its callback lifetime. A successful wire
 * exchange consumes its sequence once, even on device/snapshot error. INFO
 * has session/sequence=0 and cannot mutate hardware. All other commands are
 * exact-generation and consecutive, with no wrap or replay. accepted_frames
 * reports a successful whole enqueue even if its later snapshot fails.
 * The low-output gate is kernel-supplied and cannot be enabled by a request.
 */
#ifdef __cplusplus
}
#endif
#endif
