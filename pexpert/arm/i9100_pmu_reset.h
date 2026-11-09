#ifndef I9100_PMU_RESET_H
#define I9100_PMU_RESET_H

/* Same Exynos4210/S-Boot sequence as the hardware-verified i9ctl reset.
 * Only PMU registers are written. Caller disables IRQs and must not return
 * after SWRESET; filesystem shutdown belongs before this final hook. */
#ifndef I9100_PMU_RESET_WRITE
#define I9100_PMU_RESET_WRITE(base, offset, value) \
    (*(volatile unsigned int *)((base) + (offset)) = (value))
#endif
#ifndef I9100_PMU_RESET_BARRIER
#define I9100_PMU_RESET_BARRIER() __asm__ volatile("dsb sy" ::: "memory")
#endif
static void i9100_pmu_reset_registers(vm_offset_t base, int download)
{
    I9100_PMU_RESET_BARRIER();
    I9100_PMU_RESET_WRITE(base, 0x808, 0x12345678U);
    I9100_PMU_RESET_WRITE(base, 0x80c,
        download == 1 ? 0x12345671U : 0x12345670U);
    I9100_PMU_RESET_BARRIER();
    I9100_PMU_RESET_WRITE(base, 0x400, 1U);
    I9100_PMU_RESET_BARRIER();
}

#endif
