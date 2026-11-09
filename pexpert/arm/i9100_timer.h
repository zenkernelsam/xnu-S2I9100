/* i9100 XNU timer API (Exynos 4210, Galaxy S2 GT-I9100). */
#ifndef IOS7LAB_I9100_TIMER_H
#define IOS7LAB_I9100_TIMER_H
#include <mach/mach_types.h>

#define I9100_MCT_HZ 24000000U
/* MCT global comparator 0 on Exynos 4210 is SPI 57 -> INTID 89 (SPI + 32). */
#define I9100_MCT_IRQ 89U /* SPI 57 + 32. */
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
int I9100_timebase_initialized(void);
uint64_t I9100_panic_ticks(void);
uint64_t I9100_timer_value(void);
void I9100_timer_enabled(int enable);
void I9100_timer_snapshot(struct I9100TimerSnapshot *out);

extern uint64_t clock_decrementer;

#endif
