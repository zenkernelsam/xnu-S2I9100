/* SPDX-License-Identifier: BSD-2-Clause; actual MMIO boundary, never a facade. */
#include <stdint.h>
#include <IOKit/IOLib.h>
extern "C" uint32_t i9100_ring_mmio_read(volatile uint8_t *base,unsigned offset)
{
 OSSynchronizeIO();uint32_t value=*(volatile uint32_t *)(base+offset);OSSynchronizeIO();return value;
}
extern "C" void i9100_ring_mmio_write(volatile uint8_t *base,unsigned offset,uint32_t value)
{
 OSSynchronizeIO();*(volatile uint32_t *)(base+offset)=value;OSSynchronizeIO();
}
