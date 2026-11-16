/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_BCM_PUMP_H
#define I9100_BCM_PUMP_H
#include "i9100_bcm_boot.h"
#include "i9100_wlan_control.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_pump_error {I9100_PUMP_ARGUMENT=-1200,I9100_PUMP_DISABLED=-1201,I9100_PUMP_BUSY=-1202,
 I9100_PUMP_IO=-1203,I9100_PUMP_PROTOCOL=-1204,I9100_PUMP_HALTED=-1205,I9100_PUMP_CANCELLED=-1206};
struct i9100_pump_io {
 void *cookie;int (*lock)(void *);void (*unlock)(void *);
 int (*ethernet)(void *,const uint8_t *,unsigned bytes,unsigned priority); /* 0 copied,1 dropped,negative fatal */
};
struct i9100_bcm_pump {
 struct i9100_bcm_boot *boot;struct i9100_wlan_control *control;struct i9100_pump_io io;uintptr_t self;
 uint32_t initialized,enabled,ready,closing,bound,generation,fault,pending_rx;
 uint32_t ticks,rx_frames,data_frames,dropped_frames,event_frames,control_frames,empty_reads,budget_yields;
 uint32_t interrupt_status,mailbox,mailbox_count,flow_updates,stage;
 int32_t error,cleanup_error,quarantine_error;uint8_t frame[2048];
};
int i9100_bcm_pump_init(struct i9100_bcm_pump *,int enabled,struct i9100_bcm_boot *,struct i9100_wlan_control *,const struct i9100_pump_io *);
int i9100_bcm_pump_tick(struct i9100_bcm_pump *,unsigned max_reads);
int i9100_bcm_pump_recheck(struct i9100_bcm_pump *,unsigned *work_pending);
int i9100_bcm_pump_abort(struct i9100_bcm_pump *);
int i9100_bcm_pump_close(struct i9100_bcm_pump *);
/* Default OFF; only the pinned SDIO HMB RX mode0, no hostintmask/IRQ enable.
 * Init acquires software RX/consumer ownership but does no card IO. Keep a
 * real native session hold throughout FW/core lifetime, and an extra worker
 * hold until timer/IRQ sources have stopped and this pump has closed.
 * One serial thread/gate owns controller operations and frame delivery.
 * Poll HMB/flow/mail before F2, ack observed events before mailbox ACK, carry
 * pending FIFO work across a bounded1..16 reads until an actual zero-header.
 * Idle without frame indication never reads FIFO. Data alone honors global
 * FC pause; the reserved control slot remains available for reply progress.
 * Callback must copy synchronously; frame views and secrets are erased at
 * tick end. No failed frame replay, implicit NAK/FIFO reset/FW restart.
 * HALT, bus/parser/control error quarantines TX+RX and retains all owners.
 * Close is explicit, only IDLE/FAULT, and cannot clear ownership in-flight.
 * The caller disconnects/finishes mask restoration or explicitly aborts,
 * stops/removes all event sources, closes pump/control, then halts FW.
 * Polling/model success is not hardware, IRQ or iOS network registration. */
#ifdef __cplusplus
}
#endif
#endif
