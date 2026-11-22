/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MODEM_CP_H
#define I9100_MODEM_CP_H
#include <stdint.h>
#include "i9100_gpio_broker.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_cp_error { I9100_CP_ARGUMENT=-930,I9100_CP_DISABLED=-931,I9100_CP_BUSY=-932,
 I9100_CP_IO=-933,I9100_CP_TIMEOUT=-934,I9100_CP_OWNER=-935,I9100_CP_STATE=-936 };
enum i9100_cp_state { I9100_CP_OFF=0,I9100_CP_BOOTING=1,I9100_CP_FAILED=2,I9100_CP_CLOSING=3 };
enum i9100_cp_op { I9100_CP_OP_NONE=0,I9100_CP_OP_ON=1,I9100_CP_OP_RESET=2,
 I9100_CP_OP_OFF=3,I9100_CP_OP_WAKE=4 };
struct i9100_modem_cp_io {
 void *cookie;
 int (*lock)(void *); /* zero=acquired, IRQs remain enabled */
 void (*unlock)(void *);
 uint64_t (*now_ns)(void *);
 int (*delay_us)(void *,uint32_t); /* bounded inline delay for <=250us gaps */
};
struct i9100_modem_cp_pins {
 uint32_t seq;uint64_t time_ns;
 uint8_t cp_on,cp_reset,req_reset,pda_active,slave_wake;
 uint8_t phone_active,host_wake,dump_int,suspend_req;
};
struct i9100_modem_cp_status {
 uint32_t initialized,enabled,ready,state,closing,fault,configured,pins_owned;
 uint32_t op,step,poll,stage,seq,cancels;
 uint64_t op_started_ns,due_ns,last_ns;
 int32_t error,cleanup_error;
};
struct i9100_modem_cp {
 struct i9100_modem_cp_io io;
 struct i9100_gpio_broker *gpio;
 struct i9100_gpio_lease gpc1,gpx1,gpy4;
 uintptr_t self;
 uint32_t initialized,enabled,ready,closing,fault,state,configured,stage;
 uint32_t op,step,poll,pending,stalled,seq,cancels;
 uint64_t due_ns,op_started_ns,last_ns,prev_tick_ns;
 int32_t error,cleanup_error;
};
int i9100_modem_cp_init(struct i9100_modem_cp *,int enabled,struct i9100_gpio_broker *,
 const struct i9100_modem_cp_io *,int verified);
int i9100_modem_cp_start(struct i9100_modem_cp *,enum i9100_cp_op);
int i9100_modem_cp_tick(struct i9100_modem_cp *);
int i9100_modem_cp_cancel(struct i9100_modem_cp *);
int i9100_modem_cp_poll(struct i9100_modem_cp *,struct i9100_modem_cp_pins *);
int i9100_modem_cp_status(struct i9100_modem_cp *,struct i9100_modem_cp_status *);
int i9100_modem_cp_close(struct i9100_modem_cp *);
/* Default OFF. EUR U1 XMM6260 control pins only: GPC1.1 CP_ON, GPX1.0
 * SLAVE_WAKE/.1 HOST_WAKE/.2 DUMP/.3 SUSPEND/.4 CP_RESET/.6 PHONE_ACTIVE,
 * GPY4.2 PDA_ACTIVE/.6 RESET_REQ. GPY3 (LINK_ACTIVE) stays board-reserved;
 * USB_SEL/MUIC/safeout, EHCI/PHY/rail/IRQ registration and reverse-bias ops
 * are later stages, so this is NOT the complete vendor power path.
 * Vendor timing: on=160us,500ms,160us,160us,60us pulse,20ms; reset=
 * 20ms,160us,100us,60us,20ms; off=CP_ON+CP_RESET low; wake=slave_wake high
 * then poll hostwake(active-low) up to 20x5ms. Waits over 250us are deadline
 * steps ticked without holding the lock; shorter gaps use the checked inline
 * delay. PHONE_ACTIVE high means BOOTING in the chosen vendor handler; this
 * module never claims ONLINE, IPC, USB enumeration or firmware state.
 * Initially every output must be low and inputs must read the cold state,
 * otherwise the unmodified leases are released without touching a possibly
 * running CP. Errors latch fault and retain leases for explicit same-owner
 * cleanup; close drives owned outputs low then KEEP-releases them. Poll is
 * a firmware-independent level snapshot with seq+time, never an auto-reset.
 * IRQ-disabled callers are rejected by the native locks. Not IRQ-safe. */
#ifdef __cplusplus
}
#endif
#endif
