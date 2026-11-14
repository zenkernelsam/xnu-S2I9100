/* Original first-media self-kill snapshot before task teardown. This is an
 * ordinary own-task syscall observer, not a scheduler/IRQ callback. The board
 * runs one CPU; briefly masked raw wired reads sample saved registers, then
 * all user copyin happens after restoring the incoming masks. No suspension,
 * task/thread/port locks, allocation, syscall result or signal change. */
#ifndef I9100_MEDIA_THREADS_H
#define I9100_MEDIA_THREADS_H
#define I9100_MEDIA_THREADS_CAP 16U
#define I9100_MEDIA_THREADS_STRIDE 72U
extern volatile uint32_t i9100_media_threads[16+16*72];
void i9100_media_threads_note(unsigned,unsigned);
#endif
