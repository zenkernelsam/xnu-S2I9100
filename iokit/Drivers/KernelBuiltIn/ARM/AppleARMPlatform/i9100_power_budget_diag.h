#ifndef I9100_POWER_BUDGET_DIAG_H
#define I9100_POWER_BUDGET_DIAG_H
#include <stdint.h>

/* Fixed scalar snapshots, no addresses to probe and no new hardware owner.
 * CPU publication belongs to the Power workloop. Display publication uses
 * the existing graphics diagnostic guard after releasing transactionLock. */
#define I9100_POWER_BUDGET_STRIDE 20U
#define I9100_POWER_BUDGET_CAPACITY 16U
#define I9100_POWER_BUDGET_WORDS (4U + I9100_POWER_BUDGET_STRIDE * I9100_POWER_BUDGET_CAPACITY)

static void i9100_power_budget_publish(volatile uint32_t *table,
    const uint32_t words[I9100_POWER_BUDGET_STRIDE - 1U])
{
    unsigned sequence = table[1] + 1U;
    if (!sequence) sequence = 1U;
    volatile uint32_t *row = table + 4U +
        ((sequence - 1U) & (I9100_POWER_BUDGET_CAPACITY - 1U)) * I9100_POWER_BUDGET_STRIDE;
    row[0] = 0;
    for (unsigned n = 1; n < I9100_POWER_BUDGET_STRIDE; n++) row[n] = words[n - 1U];
    __sync_synchronize();
    row[0] = sequence;
    __sync_synchronize();
    table[1] = sequence;
}
#endif
