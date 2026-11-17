/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_CLOCK_GATES_H
#define I9100_CLOCK_GATES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define I9100_CLOCK_MAX_GATES 32U
#define I9100_CLOCK_MAX_LEASES 64U
enum i9100_clock_error {I9100_CLOCK_ARGUMENT=-280,I9100_CLOCK_DISABLED=-281,
 I9100_CLOCK_BUSY=-282,I9100_CLOCK_OWNER=-283,I9100_CLOCK_IO=-284,I9100_CLOCK_EXHAUSTED=-285};
struct i9100_clock_io {void *cookie;uint32_t (*read32)(void *,unsigned);void (*write32)(void *,unsigned,uint32_t);int (*lock)(void *);void (*unlock)(void *);};
struct i9100_clock_gate_config {uint16_t offset;uint32_t mask;};
struct i9100_clock_gate {struct i9100_clock_gate_config config;uint32_t refs,initial,fault;};
struct i9100_clock_slot {uint32_t token,gate;};
struct i9100_clock_gates {struct i9100_clock_io io;struct i9100_clock_gate gates[I9100_CLOCK_MAX_GATES];struct i9100_clock_slot leases[I9100_CLOCK_MAX_LEASES];uint32_t count,enabled,generation;};
struct i9100_clock_lease {struct i9100_clock_gates *manager;uint32_t slot,token,active;};
int i9100_clock_init(struct i9100_clock_gates *,const struct i9100_clock_io *,const struct i9100_clock_gate_config *,unsigned);
int i9100_clock_enable(struct i9100_clock_gates *,int);
int i9100_clock_acquire(struct i9100_clock_gates *,unsigned,struct i9100_clock_lease *);
int i9100_clock_release(struct i9100_clock_lease *);
int i9100_clock_check(struct i9100_clock_lease *);
int i9100_clock_recover_idle(struct i9100_clock_gates *,unsigned gate);
/* check is read-only. Recovery clears a failed acquisition only when no
 * refs/lease remain and the hardware bit is again at its saved initial
 * state; it never writes a clock or supplies a frequency measurement. */
/* Zeroed objects; gate-only CMU operations. No CPU/MPLL reprogramming or
 * claimed frequency. Last release preserves a pre-existing enabled gate. */
#ifdef __cplusplus
}
#endif
#endif
