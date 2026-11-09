#ifndef I9100_IRQ_SNAPSHOT_H
#define I9100_IRQ_SNAPSHOT_H
/* Bounded kernel-RAM reads for the UP Exynos interrupt-side observer.
 * ATS1CPR tests the active hardware translation without pmap/VM locks.
 * The caller holds IRQ/FIQ masked; sampled zone objects are wired, but their
 * lifetime/fields can still be stale. Failure means unavailable, not empty. */
#ifndef I9100_SNAPSHOT_PAGE_OK
static int i9100_snapshot_page_ok(unsigned address)
{
    unsigned saved, translated;
    __asm__ volatile("mrc p15,0,%0,c7,c4,0\n\t"
        "mcr p15,0,%2,c7,c8,0\n\tisb sy\n\t"
        "mrc p15,0,%1,c7,c4,0\n\t"
        "mcr p15,0,%0,c7,c4,0\n\tisb sy"
        : "=&r"(saved), "=&r"(translated) : "r"(address) : "memory");
    unsigned physical = translated & 0xfffff000U;
    return !(translated & 1U) && physical >= 0x40000000U && physical < 0x80000000U;
}
#define I9100_SNAPSHOT_PAGE_OK(a) i9100_snapshot_page_ok(a)
#endif
#ifndef I9100_SNAPSHOT_MASK_GET
#define I9100_SNAPSHOT_MASK_GET(mask) \
    __asm__ volatile("mrs %0,cpsr\n\tcpsid if" : "=r"(mask) :: "memory")
#define I9100_SNAPSHOT_MASK_SET(mask) \
    __asm__ volatile("msr cpsr_c,%0" :: "r"(mask) : "memory")
#endif
static __attribute__((noinline)) int
i9100_irq_snapshot_read(const void *source, void *destination, unsigned size)
{
    unsigned address = (unsigned)(uintptr_t)source;
    if (!destination || !size || size > 256 || address < 0x80000000U ||
        address >= 0xf0000000U || size > 0xf0000000U-address) return 0;
    unsigned saved_mask;
    I9100_SNAPSHOT_MASK_GET(saved_mask);
    unsigned end = address+size-1U;
    int valid = 1;
    for (unsigned page = address & ~4095U;; page += 4096U) {
        if (!I9100_SNAPSHOT_PAGE_OK(page)) { valid = 0; break; }
        if (page == (end & ~4095U)) break;
    }
    if (valid) memcpy(destination, source, size);
    I9100_SNAPSHOT_MASK_SET(saved_mask);
    return valid;
}
#endif
