/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MODEM_CP_NATIVE_H
#define I9100_MODEM_CP_NATIVE_H
#include "i9100_modem_cp.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct i9100_modem_cp_native i9100_modem_cp_native;
int i9100_modem_cp_native_open(int enabled,i9100_peripheral_native *parent,int verified,
 i9100_modem_cp_native **out);
int i9100_modem_cp_native_start(i9100_modem_cp_native *,enum i9100_cp_op);
int i9100_modem_cp_native_tick(i9100_modem_cp_native *);
int i9100_modem_cp_native_cancel(i9100_modem_cp_native *);
int i9100_modem_cp_native_poll(i9100_modem_cp_native *,struct i9100_modem_cp_pins *);
int i9100_modem_cp_native_status(i9100_modem_cp_native *,struct i9100_modem_cp_status *);
int i9100_modem_cp_native_close(i9100_modem_cp_native **);
/* Single controller instance, default OFF and opt-in only: a live parent
 * hold keeps the broker alive; the core GPIO leases stay inside this object
 * and are never exported, so no borrower can copy a lease into hardware
 * writes. Open fails on a second controller, a wrong/unverified parent, a
 * claimed or reserved pin, or any non-cold pin level; partial claim/config
 * failures stay releasable. Close stops new control writes, drives owned
 * outputs low, KEEP-releases the leases, then puts the parent hold; a failed
 * close retains everything for an explicit retry. No IRQ, EHCI, PHY, USB
 * path, firmware or telephony behavior is implied. */
#ifdef __cplusplus
}
#endif
#endif
