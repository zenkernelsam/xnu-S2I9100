/* I9100 XNU timer API. Register layout notes only; no foreign code imported. */
#ifndef IOS7LAB_I9100_TIMER_H
#define IOS7LAB_I9100_TIMER_H
#include <mach/mach_types.h>
#define I9100_MCT_HZ 24000000U
/* Exynos 4210 MCT global comparator 0: the SoC model splits it straight to
 * the external GIC (EXT_GIC_ID_MCT_G0), same pin Linux's exynos4210.dtsi
 * lists as SPI 3 -> INTID 35. Combiner group-1-bit-4 also sees it but the
 * direct ext-GIC path needs no combiner programming under QEMU smdkc210. */
#define I9100_MCT_IRQ 35U /* SPI 3 + 32. */
#define I9100_MCT_WRITE_DELAY_TICKS 64U
#define I9100_MCT_MIN_MATCH_TICKS (I9100_MCT_WRITE_DELAY_TICKS+4U)
struct I9100TimerSnapshot {
    uint32_t version, valid_flags, count_lo, match_lo, enable, irq_status;
    uint32_t irq_count, last_irq_counter, last_vector, spurious_irq_count;
    uint32_t frequency_hz, minimum_match_ticks;
    uint64_t extended_ticks;
};
typedef char I9100TimerSnapshot_size[(sizeof(struct I9100TimerSnapshot)==56)?1:-1];
void I9100_interrupt_init(void);
void I9100_timebase_init(void);
void I9100_handle_interrupt(void *context);
uint64_t I9100_get_timebase(void);
uint64_t I9100_timer_value(void);
void I9100_timer_enabled(int enable);
void I9100_timer_snapshot(struct I9100TimerSnapshot *out);
extern uint64_t clock_decrementer;
#endif
