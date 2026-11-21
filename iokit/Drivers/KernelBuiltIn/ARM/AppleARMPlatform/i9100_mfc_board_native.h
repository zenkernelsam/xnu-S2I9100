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
struct i9100_mfc_board_irq_token {uintptr_t self,board;uint32_t generation,owned;};
int i9100_mfc_board_open(int enabled,struct i9100_peripheral_native *,const struct i9100_mfc_power_config *,struct i9100_mfc_board **);
int i9100_mfc_board_supply(struct i9100_mfc_board *,struct i9100_mfc_supply *);
int i9100_mfc_board_status(struct i9100_mfc_board *,struct i9100_mfc_board_status *);
int i9100_mfc_board_irq_acquire(struct i9100_mfc_board *,void (*notify)(void *),void *cookie,struct i9100_mfc_board_irq_token *);
int i9100_mfc_board_irq_mask(struct i9100_mfc_board *,const struct i9100_mfc_board_irq_token *);
int i9100_mfc_board_irq_unmask(struct i9100_mfc_board *,const struct i9100_mfc_board_irq_token *);
int i9100_mfc_board_irq_enabled(struct i9100_mfc_board *,const struct i9100_mfc_board_irq_token *);
int i9100_mfc_board_irq_line(struct i9100_mfc_board *,const struct i9100_mfc_board_irq_token *,unsigned *enabled,unsigned *active,unsigned *dispatch);
int i9100_mfc_board_irq_ack(struct i9100_mfc_board *,const struct i9100_mfc_board_irq_token *);
int i9100_mfc_board_irq_release(struct i9100_mfc_board *,struct i9100_mfc_board_irq_token *);
/* Provider-side control identity: the caller hands over only the opaque
 * token self it was issued; the board compares it against the token it
 * actually owns without ever dereferencing caller integers. Returns the
 * issuing generation when the identity matches. */
int i9100_mfc_board_irq_verify(struct i9100_mfc_board *,uintptr_t token_self,unsigned *generation);
/* Owner-only repair for a faulted registry slot: the exact bound owner
 * proves the line masked and the dispatch/EOI window quiet, resolving the
 * latched recorded-vs-actual mismatch so normal teardown paths serve again.
 * The binding stays; the fault counters keep the original diagnostics. */
int i9100_mfc_board_irq_repair(struct i9100_mfc_board *,struct i9100_mfc_board_irq_token *);
/* Owner-only cleanup for a faulted registry slot: normal commands keep
 * refusing through the token gate, but the exact owner may still mask the
 * line, prove dispatch/active quiet and release its binding while the slot
 * keeps its original fault diagnostics. Never unmask, never codec. */
int i9100_mfc_board_irq_cleanup(struct i9100_mfc_board *,struct i9100_mfc_board_irq_token *);
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
 * outlive a successfully destroyed board. IRQ stays masked for the polling
 * path. An exact single irq-owner token may bind one notify-only sink and
 * mask/unmask the leased line; the ISR verifies its own MFC source latch,
 * deasserts it, masks the line and notifies, never IAR/EOI. Board close
 * refuses a bound irq owner. No GIC reset, IRQ EOI, arbitrary MMIO mapping
 * or gain/RF writes. */
#ifdef __cplusplus
}
#endif
#endif
