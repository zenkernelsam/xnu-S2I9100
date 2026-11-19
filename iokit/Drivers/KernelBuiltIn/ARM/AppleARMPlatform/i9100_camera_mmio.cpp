/* SPDX-License-Identifier: BSD-2-Clause; actual MMIO boundary. */
#include <stdint.h>
#include <IOKit/IOLib.h>
extern "C" uint32_t i9100_camera_mmio_read(volatile uint8_t *base,unsigned offset)
{
 OSSynchronizeIO();uint32_t v=*(volatile uint32_t *)(base+offset);OSSynchronizeIO();return v;
}
extern "C" void i9100_camera_mmio_write(volatile uint8_t *base,unsigned offset,uint32_t value)
{
 OSSynchronizeIO();*(volatile uint32_t *)(base+offset)=value;OSSynchronizeIO();
}
