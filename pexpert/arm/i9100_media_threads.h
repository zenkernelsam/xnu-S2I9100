/* Original first-media self-kill snapshot before task teardown. This is an
 * ordinary own-task syscall observer, not a scheduler/IRQ callback. The board
 * runs one CPU; briefly masked raw wired reads sample saved registers, then
 * all user copyin happens after restoring the incoming masks. No suspension,
 * task/thread/port locks, allocation, syscall result or signal change. */
#ifndef I9100_MEDIA_THREADS_H
#define I9100_MEDIA_THREADS_H
#include "i9100_audio_observation_target.h"
#define I9100_MEDIA_THREADS_CAP 16U
#define I9100_MEDIA_THREADS_STRIDE 72U
extern volatile uint32_t i9100_media_threads[16+16*72];
/* Parallel rows keyed to the published peer snapshot; preserve its v2 ABI.
 * Raw cthread_self is uint64_t. A TLS first word is evidence, not an assumed
 * pthread identity. The mutex prefix belongs only to that row's wait. */
#define I9100_MEDIA_IDENTITY_STRIDE 8U
extern volatile uint32_t i9100_media_identity[8+16*8];
void i9100_media_threads_note(unsigned,unsigned);
#define I9100_AUDIO_OWNER_CAP 8U
#define I9100_AUDIO_OWNER_STRIDE 96U
#define I9100_AUDIO_OWNER_FRAMES 16U
extern volatile uint32_t i9100_audio_owner_diag[4+8*96];
extern volatile uint32_t i9100_audio_owner_stack[16+64];
extern volatile uint32_t i9100_media_dispatch_diag[16+64];
void i9100_audio_owner_note(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
#endif
