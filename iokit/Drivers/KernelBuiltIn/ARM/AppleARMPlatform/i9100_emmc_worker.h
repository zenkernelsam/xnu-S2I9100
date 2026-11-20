/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_WORKER_H
#define I9100_EMMC_WORKER_H
#include "i9100_emmc_native.h"
#include <IOKit/storage/IOStorage.h>
struct i9100_emmc_worker;
enum i9100_emmc_work_operation {I9100_EMMC_WORK_READ=1,I9100_EMMC_WORK_WRITE=2,
 I9100_EMMC_WORK_SYNC=3,I9100_EMMC_WORK_SEAL=4};
typedef IOReturn (*i9100_emmc_work_validate)(OSObject *,UInt64,unsigned);
struct i9100_emmc_worker_status {
 uint64_t submitted,completed,cancelled,read_bytes,written_bytes;
 uint32_t queued,active,callbacks,stopping,fault,closed,held,quarantined;
 int error,cleanup_error;
};
int i9100_emmc_worker_create(int enabled,struct i9100_emmc_native *,struct i9100_emmc_worker **);
IOReturn i9100_emmc_worker_submit(struct i9100_emmc_worker *,OSObject *owner,UInt64 token,
 i9100_emmc_work_validate,unsigned operation,UInt64 absolute_lba,UInt64 sectors,
 IOMemoryDescriptor *,const IOStorageCompletion *);
IOReturn i9100_emmc_worker_barrier(struct i9100_emmc_worker *,OSObject *owner,UInt64 token,
 i9100_emmc_work_validate,unsigned operation); /* ordered sync/seal; caller sleeps, never the workloop */
void i9100_emmc_worker_cancel(struct i9100_emmc_worker *,OSObject *owner,UInt64 token);
IOReturn i9100_emmc_worker_stop(struct i9100_emmc_worker *);
int i9100_emmc_worker_destroy(struct i9100_emmc_worker **);
int i9100_emmc_worker_snapshot(struct i9100_emmc_worker *,struct i9100_emmc_worker_status *);
/* Scoped eMMC only, default-off create. Bounded16requests/1MiB each, 16KiB
 * transport chunks, descriptor prepare/complete and retained owners. Submit
 * never performs MMC IO. Cancellation drains an active checked chunk and
 * prevents subsequent chunks; queued callbacks still run exactly once.
 * Native owns DMA buffers, card deadlines/dirty/error and real host lease.
 * Callback runs with queue/native/workloop locks released. Sync/seal are
 * FIFO barriers, never an overtaking direct command. Failed drain/removal
 * keeps the queue/native hold in an internal retained-fault list. */
#endif
