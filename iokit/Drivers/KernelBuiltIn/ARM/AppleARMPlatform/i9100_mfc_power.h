/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_POWER_H
#define I9100_MFC_POWER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_mfc_power_region {MP_CMU,MP_PMU,MP_CHIP,MP_MFC,MP_MMU_L,MP_MMU_R,MP_GIC,MP_REGIONS};
enum i9100_mfc_power_error {I9100_MP_ARGUMENT=-880,I9100_MP_DISABLED=-881,I9100_MP_BUSY=-882,
 I9100_MP_IO=-883,I9100_MP_TIMEOUT=-884,I9100_MP_OWNER=-885,I9100_MP_CLOCK=-886};
enum i9100_mfc_power_field {MP_CHIP_ID,MP_CPU_SOURCE,MP_MPLL0,MP_MPLL1,MP_SOURCE,MP_DIVIDER,MP_DIV_BUSY,
 MP_GATE,MP_CONFIG,MP_STATUS,MP_IRQ_ENABLE,MP_IRQ_ACTIVE,MP_FIELD_COUNT};
struct i9100_mfc_power_io {
 void *cookie;uint32_t (*read32)(void *,unsigned,unsigned);void (*write32)(void *,unsigned,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);uint64_t (*now_ns)(void *);int (*delay_us)(void *,uint32_t);void (*barrier)(void *);
 int (*exclusive)(void *); /* actual native reservation + exact masked IRQ lease */
 int (*consumers_idle)(void *); /* actual engine/allocations/caller leases, no guessed idle */
};
struct i9100_mfc_power_config {uint32_t board_data_reviewed,xxti_hz,maximum_clock_hz;};
struct i9100_mfc_power {
 uintptr_t self;struct i9100_mfc_power_io io;struct i9100_mfc_power_config config;
 uint32_t initialized,enabled,owned,ready,fault,closing,generation;
 uint32_t saved_gate,saved_power,saved_source,saved_divider;
 uint32_t gate_written,power_written,source_written,divider_written,working_divider,previous_divider;
 uint32_t divider,parent_hz,clock_hz,cpu_source,mpll0,confirm_reset,confirm_mc;
 uint32_t before[MP_FIELD_COUNT],after[MP_FIELD_COUNT];int error,cleanup_error;
};
unsigned i9100_mfc_power_region(unsigned field);
unsigned i9100_mfc_power_offset(unsigned field);
int i9100_mfc_power_parent(uint32_t xxti_hz,uint32_t cpu_source,uint32_t mpll0,uint32_t maximum_hz,
 uint32_t *parent_hz,uint32_t *divider,uint32_t *clock_hz);
int i9100_mfc_power_init(struct i9100_mfc_power *,const struct i9100_mfc_power_io *,const struct i9100_mfc_power_config *);
int i9100_mfc_power_enable(struct i9100_mfc_power *,int);
int i9100_mfc_power_acquire(struct i9100_mfc_power *);
int i9100_mfc_power_validate(struct i9100_mfc_power *);
int i9100_mfc_power_release(struct i9100_mfc_power *);
/* Exynos4210 MFCv5.1: exact reservation, CHIP identity, PMU3c40/3c44,
 * CMUc928 bits0/1/2 only, leafc228 bits0/8 -> current mout_mpll, c528[3:0].
 * MPLL45xx root and CPU source are read twice; no global PLL/CPU changes.
 * XXTI24MHz is explicit board evidence, not a frequency measurement.
 * Temporary local clocks precede PMU power-on; powered MFC read confirms
 * completion. Neither MFC nor MMUs are read while their domain/gates are off.
 * Source/divider changes require actual RISC/MC/mailbox/MMU quiescence.
 * Cold and stable warm states are supported; no foreign reset, blanket
 * gate writes, PMU OPTION, IRQ acknowledgement, suspend or force cleanup.
 * Error records/partial ownership remain until explicit release succeeds.
 * Release first checks consumer lifetimes and idle before local restoration
 * and optional power-off. Already-powered inherited state is preserved.
 * Validation may succeed after hardware restoration solely to permit safe
 * cleanup; native retains its original error and blocks new codec commands.
 * Single caller-owned address; all operational APIs serialize with lock.
 * This module never runs firmware, emits audio or contacts a device by itself. */
#ifdef __cplusplus
}
#endif
#endif
