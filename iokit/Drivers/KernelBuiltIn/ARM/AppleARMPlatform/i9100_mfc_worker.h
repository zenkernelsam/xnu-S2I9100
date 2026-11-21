/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_WORKER_H
#define I9100_MFC_WORKER_H
#include "i9100_mfc_irq.h"
#include "i9100_mfc_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_mfc_worker;
enum {I9100_MFC_WORKER_QUEUE=8,I9100_MFC_WORKER_INPUT_MAX=1024U*1024U,
 I9100_MFC_WORKER_DEFERS=4096};
enum {I9100_MFC_WORK_BOOT=1,I9100_MFC_WORK_FRAME=2};
typedef void (*i9100_mfc_worker_done)(void *owner,uint64_t token,int result);
struct i9100_mfc_worker_status {
 uint64_t submitted,completed,cancelled,interrupts,defers,late_irq,timeouts;
 uint32_t queued,in_flight,armed,callbacks,stopping,fault,closed,held,irq_bound,ctl_bound,cancel_pending;
 int error,cleanup_error;
};
int i9100_mfc_worker_create(int enabled,struct i9100_mfc_native *,struct i9100_mfc_board *,struct i9100_mfc_worker **);
int i9100_mfc_worker_boot(struct i9100_mfc_worker *,void *owner,uint64_t token,
 const struct i9100_mfc_firmware_profile *,const void *firmware,uint32_t firmware_bytes,
 const void *stream,uint32_t stream_bytes,i9100_mfc_worker_done);
int i9100_mfc_worker_frame(struct i9100_mfc_worker *,void *owner,uint64_t token,
 const void *annexb,uint32_t bytes,uint32_t tag,int last,i9100_mfc_worker_done);
void i9100_mfc_worker_cancel(struct i9100_mfc_worker *,void *owner,uint64_t token);
int i9100_mfc_worker_frame_lease(struct i9100_mfc_worker *,struct i9100_mfc_frame_lease *);
int i9100_mfc_worker_frame_return(struct i9100_mfc_worker *,struct i9100_mfc_frame_lease *);
int i9100_mfc_worker_stop(struct i9100_mfc_worker *);
int i9100_mfc_worker_destroy(struct i9100_mfc_worker **);
int i9100_mfc_worker_snapshot(struct i9100_mfc_worker *,struct i9100_mfc_worker_status *);
/* Default OFF. Real asynchronous MFC path: the board-held IRQ126 upper half
 * notifies only; this IOWorkLoop thread performs actual R2H/ARG/expected
 * rereads through the existing native/core polling logic, then re-arms the
 * exact leased line. One hardware command in flight over a bounded FIFO;
 * submit copies private input and returns without waiting. Completion,
 * cancel and fault each finish a request exactly once with callbacks run
 * outside the workloop gate. An issued command whose bounded client
 * deadline fires is told exactly once but stays internally owned and
 * counted: the real IRQ/poll service continues until the engine's reply,
 * its own deadline fault, or a proven exact-owner shutdown retires the
 * hardware side — never a detached finish while the mailbox still reports
 * the command. The timer only services pending deadlines, lost IRQs and
 * bounded defers; it never fabricates a firmware reply. Backpressure waits
 * on actual frame-lease release events instead of spinning. The worker
 * keeps an exact native control lease bound to the board-issued IRQ token
 * plus live supply generation; raw command entry is refused while it lives,
 * a clock/power domain loss is latched as a real fault instead of reading
 * the dead domain, and a failed stop/removal retains all ownership for the
 * caller's explicit same-stage retry. Offline models exercise this control
 * flow but are not IRQ delivery proof. */
#ifdef __cplusplus
}
#endif
#endif
