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
struct i9100_power_haptic_ops {
 int (*read_config)(void *power_hw,uint8_t *);
 int (*write_config)(void *power_hw,uint8_t);
 uint32_t guarded_wire;
};
/* Fixed callbacks provided only by the paired, filtered raw-bus build. */
int i9100_power_haptic_config_read(void *power_hw,uint8_t *);
int i9100_power_haptic_config_write(void *power_hw,uint8_t);
struct i9100_power_haptic_client {
 void *context;uintptr_t self;uint32_t generation,owned,busy,ready,fault,users;
 uint32_t rail_owned,motor_owned;uint8_t saved_config,saved_ldo;
 int32_t error,cleanup_error;
};
struct i9100_power_haptic_hold {struct i9100_power_haptic_client *client;uintptr_t self;uint32_t generation,owned;};
int i9100_power_shared_prepare(int enabled);
int i9100_power_shared_enabled(void);
int i9100_power_bus_enter(unsigned bus,struct i9100_power_bus_ticket *);
int i9100_power_bus_leave(struct i9100_power_bus_ticket *);
int i9100_power_bus_filter(struct i9100_power_bus_ticket *,unsigned slave,unsigned reg,const uint8_t *data,unsigned count,int writing);
int i9100_power_shared_publish(void *owner_osobject,struct i9100_power_hw *);
int i9100_power_shared_publish_haptic(void *owner_osobject,struct i9100_power_hw *,const struct i9100_power_haptic_ops *);
int i9100_power_shared_unpublish(void *owner_osobject);
int i9100_power_read_open(struct i9100_power_read_client *);
int i9100_power_read_pmic(struct i9100_power_read_client *,unsigned reg,uint8_t *);
int i9100_power_read_close(struct i9100_power_read_client *);
int i9100_power_haptic_open(struct i9100_power_haptic_client *,int verified_gt_i9100);
int i9100_power_haptic_check(struct i9100_power_haptic_client *);
int i9100_power_haptic_read(struct i9100_power_haptic_client *,uint8_t *config);
int i9100_power_haptic_write(struct i9100_power_haptic_client *,uint8_t config);
int i9100_power_haptic_rail(struct i9100_power_haptic_client *,int enabled);
int i9100_power_haptic_close(struct i9100_power_haptic_client *);
int i9100_power_haptic_hold_get(struct i9100_power_haptic_client *,struct i9100_power_haptic_hold *);
int i9100_power_haptic_hold_check(struct i9100_power_haptic_hold *);
int i9100_power_haptic_hold_put(struct i9100_power_haptic_hold *);
/* Fixed slave48 CONFIG2 and PMIC slave66 LDO14/48 only. Paired Power/raw-bus
 * integration supplies guarded CONFIG2 callbacks. Open owns both register
 * resources globally, holds the real provider, requires motor disabled and
 * the U1 2.8V selector with normal/off mode. No voltage programming, pin/IRQ
 * consumption or arbitrary PMIC writes. Foreign Power/Mali writes to those
 * resources are denied before wire IO while this lease exists.
 * Rail put restores the initial enable byte: an initially on rail stays on.
 * Revocation/fault denies new motor/rail enable but permits exact cleanup on
 * the retained old provider. PWM/worker borrowers hold until confirmed stop;
 * close requires no borrowers and original config/rail bytes read back.
 * Keep client/hold addresses until closed; copies/stale values are rejected.
 * The guarded_wire marker describes the compiled backend contract, not an
 * independent proof that a live device has the patch or measured 2.8V. */
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
 * before IO. Existing Power RTC sampling alone uses fixed BUS0/slave06
 * CONTROL03/1B and TIME10/7B read-only transfers. No external RTC client,
 * RTC write/alarm/UPDATE or extended arbitrary burst is added. All use the
 * original shared gate; epoch decoding remains in the Power policy.
 * Reads are individual transactions, not a multi-register atomic
 * electrical snapshot. No rail/LPO guarantee, GPIO/ADC redesign or change
 * to charging thresholds/current is implied. Main integration remains opt-in. */
#ifdef __cplusplus
}
#endif
#endif
