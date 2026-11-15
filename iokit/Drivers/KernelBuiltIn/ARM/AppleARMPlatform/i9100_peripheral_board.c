/* SPDX-License-Identifier: BSD-2-Clause
 * Register and wiring facts checked against pinned Samsung U1/Exynos4210
 * board, GPIO, clock and DMA sources in research/peripheral-reference-20261115.
 */
#include "i9100_peripheral_board.h"
#include <string.h>
const uint32_t i9100_gpio_physical[3] = {0x11400000U,0x11000000U,0x03860000U};
const struct i9100_gpio_bank_config i9100_u1_banks[I9100_BANK_COUNT] = {
 {0x000,0,8,0x00}, {0x020,0,6,0x0f}, {0x040,0,8,0xc0},
 {0x060,0,5,0x00}, {0x080,0,5,0x00}, {0x0a0,0,4,0x00},
 {0x0c0,0,4,0x00}, {0x0e0,0,5,0x00}, {0x100,0,8,0x0f},
 {0x120,0,6,0x00}, {0x140,0,8,0x00}, {0x160,0,8,0x00},
 {0x180,0,8,0xff}, {0x1a0,0,8,0xff}, {0x1c0,0,8,0xff},
 {0x1e0,0,6,0x3f}, {0x000,1,8,0x00}, {0x020,1,5,0x00},
 {0x040,1,7,0x7f}, {0x060,1,7,0x7f}, {0x080,1,7,0x7f},
 {0x0a0,1,7,0x00}, {0x0c0,1,8,0x08}, {0x0e0,1,3,0x00},
 {0x100,1,8,0x34}, {0x120,1,6,0x3f}, {0x140,1,4,0x0f},
 {0x160,1,6,0x3f}, {0x180,1,8,0xff}, {0x1a0,1,8,0x03},
 {0x1c0,1,8,0xff}, {0x1e0,1,8,0xff}, {0xc00,1,8,0x18},
 {0xc20,1,8,0x00}, {0xc40,1,8,0x83}, {0xc60,1,8,0x28},
 {0x000,2,7,0x00}
};
const struct i9100_clock_gate_config i9100_u1_gates[I9100_GATE_COUNT] = {
 {0xc950,1U<<7}, {0xc950,1U<<12}, {0xc950,1U<<13},
 {0xc940,1U<<0}, {0xc940,1U<<1}, {0xc950,1U<<0}, {0xc950,1U<<24}
};
const struct i9100_i2c_board_bus i9100_u1_i2c[3] = {
 {0x13870000U,I9100_GPD1,2,3,2,91,I9100_GATE_I2C1},
 {0x138c0000U,I9100_GPC1,3,4,4,96,I9100_GATE_I2C6},
 {0x138d0000U,I9100_GPD0,2,3,3,97,I9100_GATE_I2C7}
};
int i9100_u1_profile_validate(void)
{
 if(I9100_BANK_COUNT>I9100_GPIO_BANKS_MAX)return I9100_GPIO_ARGUMENT;
 for(unsigned n=0;n<I9100_BANK_COUNT;n++) {
  const struct i9100_gpio_bank_config *b=&i9100_u1_banks[n];
  if(!b->pins||b->pins>8||b->region>2||b->reserved_mask&~((1U<<b->pins)-1U))
   return I9100_GPIO_ARGUMENT;
  for(unsigned p=0;p<n;p++)
   if(b->region==i9100_u1_banks[p].region&&b->offset==i9100_u1_banks[p].offset)
    return I9100_GPIO_ARGUMENT;
 }
 for(unsigned n=0;n<3;n++) {
  const struct i9100_i2c_board_bus *b=&i9100_u1_i2c[n];
  if(b->bank>=I9100_BANK_COUNT||b->sda==b->scl||
     b->sda>=i9100_u1_banks[b->bank].pins||b->scl>=i9100_u1_banks[b->bank].pins||
     (i9100_u1_banks[b->bank].reserved_mask&((1U<<b->sda)|(1U<<b->scl))))
   return I9100_GPIO_ARGUMENT;
 }
 return 0;
}
void i9100_u1_irq_whitelist(uint32_t allowed[8])
{
 if(!allowed)return;
 memset(allowed,0,8*sizeof(*allowed));
 const unsigned sources[]={49,64,67,68,84,91,96,97,108,110,112,116,117,118,119,126,129};
 for(unsigned n=0;n<sizeof(sources)/sizeof(sources[0]);n++)
  allowed[sources[n]/32]|=1U<<(sources[n]%32);
 /* 49=gyro EINT1; 64=EINT16..31; 67/68=PDMA; 84=BT UART0;
  * 91/96/97=I2C1/6/7; 108=SDIO3; 110/112=CSI;116..119=FIMC;126=MFC;129=I2S0.
  * Active MCT89, USBCDC polling, external SD107 and touch EINT4/52 excluded.
  * Nested EINT clients also validate child reservation; a parent is not a
  * license to overwrite all of GPX. */
}
