/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_POWER_SHARED_H
#define I9100_POWER_SHARED_H
#include "i9100_power.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_shared_error {I9100_SHARED_ARGUMENT=-1300,I9100_SHARED_DISABLED=-1301,I9100_SHARED_BUSY=-1302,
 I9100_SHARED_IO=-1303,I9100_SHARED_NOT_READY=-1304,I9100_SHARED_EXHAUSTED=-1305};
struct i9100_power_bus_ticket {uintptr_t self,thread;uint32_t bus,mode,owned;};
struct i9100_power_read_client {void *context;uintptr_t self;uint32_t generation,owned,busy;};
int i9100_power_shared_prepare(int enabled);
int i9100_power_shared_enabled(void);
int i9100_power_bus_enter(unsigned bus,struct i9100_power_bus_ticket *);
int i9100_power_bus_leave(struct i9100_power_bus_ticket *);
int i9100_power_shared_publish(void *owner_osobject,struct i9100_power_hw *);
int i9100_power_shared_unpublish(void *owner_osobject);
int i9100_power_read_open(struct i9100_power_read_client *);
int i9100_power_read_pmic(struct i9100_power_read_client *,unsigned reg,uint8_t *);
int i9100_power_read_close(struct i9100_power_read_client *);
/* Default OFF. An integration patch makes ALL existing raw PMIC/MUIC/fuel
 * calls enter this shared gate, including Mali's separate hardware cookie.
 * prepare occurs before driver IO; legacy in-flight count makes a racing
 * enable fail instead of introducing a lock in the middle of a transaction.
 * Once enabled, mutexes live for the kernel lifetime and never fall back to
 * unlocked IO. Per-bus acquisition is thread-only and bounded100ms/1000steps.
 * Provider publish requires initialized Power mappings; publication and each
 * client retain the actual OSObject. Unpublish revokes future and in-flight
 * read results, while client references keep maps live through completion.
 * Read clients cannot write or consume IRQ/read-clear registers. Current
 * safe set: ID/VER, CONTROL1/2, LDO1..18/21 CTRL. All other addresses fail
 * before IO. Reads are individual transactions, not a multi-register atomic
 * electrical snapshot. No rail/LPO guarantee, GPIO/ADC redesign or change
 * to charging thresholds/current is implied. Main integration remains opt-in. */
#ifdef __cplusplus
}
#endif
#endif
