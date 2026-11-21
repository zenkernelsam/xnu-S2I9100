/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_BOARD_NATIVE_H
#define I9100_MFC_BOARD_NATIVE_H
#include "i9100_mfc_native.h"
#include "i9100_mfc_power.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_mfc_board;
struct i9100_mfc_board_status {
 uint32_t enabled,owned,ready,borrowed,generation,parent_hz,clock_hz,divider,reset,mc;
 uint32_t before[MP_FIELD_COUNT],after[MP_FIELD_COUNT];int error,cleanup_error;
};
int i9100_mfc_board_open(int enabled,struct i9100_peripheral_native *,const struct i9100_mfc_power_config *,struct i9100_mfc_board **);
int i9100_mfc_board_supply(struct i9100_mfc_board *,struct i9100_mfc_supply *);
int i9100_mfc_board_status(struct i9100_mfc_board *,struct i9100_mfc_board_status *);
int i9100_mfc_board_close(struct i9100_mfc_board **);
/* Actual IOKit PMU/CMU/CHIP/MFC/two-MMU/GIC maps and shared root reservation,
 * exact parent hold and masked GIC126 registration. Exposes the real power
 * supplier to existing native firmware/header/decode APIs; never launches a
 * firmware by opening the board. No platform hook or default activation.
 * Failed partial open retains its output for explicit close; don't reuse
 * that context as ready. One engine child/one exact supply lease only.
 * The board and caller output memory remain live until the engine, surfaces,
 * DMA and supply lease are closed. Board close refuses all such borrowers,
 * checks actual idle, restores only local owned fields and saved power state.
 * Consumer APIs serialize entry against final close; no stale callback may
 * outlive a successfully destroyed board. IRQ stays masked for this polling
 * path; no GIC reset, IRQ EOI, arbitrary MMIO mapping or gain/RF writes. */
#ifdef __cplusplus
}
#endif
#endif
