/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_AUDIO_POWER_H
#define I9100_AUDIO_POWER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_audio_power_error {I9100_AP_ARGUMENT=-1200,I9100_AP_DISABLED=-1201,
 I9100_AP_BUSY=-1202,I9100_AP_OWNER=-1203,I9100_AP_IO=-1204,I9100_AP_TIMEOUT=-1205};
struct i9100_audio_pmu_io {
 void *cookie;uint32_t (*read32)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);
 uint64_t (*now_ns)(void *);int (*delay_us)(void *,uint32_t);
 int (*lease_valid)(void *); /* zero=verified sole Exynos4210/U1 board owner */
 int (*consumers_idle)(void *); /* zero=all affected actual consumers and borrowed owners are idle */
 int (*temp_clocks_get)(void *);int (*temp_clocks_put)(void *);
 int (*confirm_bus)(void *); /* read AUDSS only after MAUDIO status==7 */
};
struct i9100_audio_mclk {
 struct i9100_audio_pmu_io io;uintptr_t self;
 uint32_t initialized,enabled,owned,ready,fault,saved,holds,verified_xtal_hz;
 int32_t error;
};
struct i9100_audio_domain {
 struct i9100_audio_pmu_io io;uintptr_t self;
 uint32_t initialized,enabled,owned,ready,fault,saved,temp_owned,holds;
 int32_t error;
};
int i9100_audio_mclk_init(struct i9100_audio_mclk *,const struct i9100_audio_pmu_io *,uint32_t verified_xusbxti_hz);
int i9100_audio_mclk_enable(struct i9100_audio_mclk *,int);
int i9100_audio_mclk_acquire(struct i9100_audio_mclk *);
int i9100_audio_mclk_rate(struct i9100_audio_mclk *,uint32_t *source_hz);
int i9100_audio_mclk_hold(struct i9100_audio_mclk *);
int i9100_audio_mclk_unhold(struct i9100_audio_mclk *);
int i9100_audio_mclk_release(struct i9100_audio_mclk *);
int i9100_audio_domain_init(struct i9100_audio_domain *,const struct i9100_audio_pmu_io *);
int i9100_audio_domain_enable(struct i9100_audio_domain *,int);
int i9100_audio_domain_acquire(struct i9100_audio_domain *);
int i9100_audio_domain_hold(struct i9100_audio_domain *);
int i9100_audio_domain_unhold(struct i9100_audio_domain *);
int i9100_audio_domain_release(struct i9100_audio_domain *);
/* Default OFF, worker mutex only. Fixed PMU DEBUG0a00/selector[11:8]=9 and
 * disable bit0; 24MHz XUSBXTI is an externally verified input, not a frequency
 * measurement. Preserve an already enabled correct source, refuse another
 * enabled source, restore only the owned mask, and wait a bounded10ms.
 *
 * MAUDIO CONFIG3cc0/STATUS3cc4 mask7 is pinned to dev-pd-exynos4/regs-pmu-4212
 * included by the shared regs-pmu header. Stable0/7 states only, bounded1ms
 * transitions and mandatory powered-bus confirmation. Never guess OPTION,
 * use MAUDIO_SYS low-power controls, or reset a global PMU/clock tree.
 * Temporary IP clocks are provided by an audited exclusive board adapter;
 * this core does not copy the vendor's blanket ffffffff clock-gate write.
 * temp_put must safely end this context's attempted temp_get, even after a
 * partial failure (idempotent if its own lease was never acquired).
 *
 * Failed acquisition/cleanup preserves owned/temp_owned/holds/context until
 * explicit release succeeds. Busy or pinned consumers cannot lose power or
 * MCLK. Callbacks, maps and source verification remain live until then.
 * A ready flag proves register protocol, not rails, MCLK waveform or audio.
 * This core never programs a codec, speaker, PMIC gain or clock divider.
 */
#ifdef __cplusplus
}
#endif
#endif
