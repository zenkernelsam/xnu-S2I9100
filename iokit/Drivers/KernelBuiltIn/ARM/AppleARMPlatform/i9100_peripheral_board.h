/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_PERIPHERAL_BOARD_H
#define I9100_PERIPHERAL_BOARD_H
#include "i9100_gpio_broker.h"
#include "i9100_clock_gates.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_bank {
 I9100_GPA0, I9100_GPA1, I9100_GPB, I9100_GPC0, I9100_GPC1,
 I9100_GPD0, I9100_GPD1, I9100_GPE0, I9100_GPE1, I9100_GPE2,
 I9100_GPE3, I9100_GPE4, I9100_GPF0, I9100_GPF1, I9100_GPF2,
 I9100_GPF3, I9100_GPJ0, I9100_GPJ1, I9100_GPK0, I9100_GPK1,
 I9100_GPK2, I9100_GPK3, I9100_GPL0, I9100_GPL1, I9100_GPL2,
 I9100_GPY0, I9100_GPY1, I9100_GPY2, I9100_GPY3, I9100_GPY4,
 I9100_GPY5, I9100_GPY6, I9100_GPX0, I9100_GPX1, I9100_GPX2,
 I9100_GPX3, I9100_GPZ, I9100_BANK_COUNT
};
enum i9100_gate {
 I9100_GATE_I2C1, I9100_GATE_I2C6, I9100_GATE_I2C7,
 I9100_GATE_PDMA0, I9100_GATE_PDMA1, I9100_GATE_UART0,
 I9100_GATE_PWM, I9100_GATE_HSMMC3, I9100_GATE_SCLK_MMC3, I9100_GATE_COUNT
};
struct i9100_i2c_board_bus {
 uint32_t physical; unsigned bank, sda, scl, function, irq, gate;
};
extern const uint32_t i9100_gpio_physical[3];
extern const struct i9100_gpio_bank_config i9100_u1_banks[I9100_BANK_COUNT];
extern const struct i9100_clock_gate_config i9100_u1_gates[I9100_GATE_COUNT];
extern const struct i9100_i2c_board_bus i9100_u1_i2c[3]; /* I2C1, I2C6, I2C7 */
int i9100_u1_profile_validate(void);
void i9100_u1_irq_whitelist(uint32_t allowed[8]);
/* EUR/GT-I9100 source profile; regional U1 variants must not reuse this
 * without board/SoC verification. No init function here performs MMIO.
 * Mainline display/touch/power/UART2/external SD/internal MMC/boot pins are
 * reserved. Drivers claim only their roles, never the complete GPIO region. */
#ifdef __cplusplus
}
#endif
#endif
