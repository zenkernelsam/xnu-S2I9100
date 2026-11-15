/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SYSMMU_H
#define I9100_SYSMMU_H
#include "i9100_dma_guard.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_sysmmu_error {I9100_MMU_ARGUMENT=-880,I9100_MMU_DISABLED=-881,I9100_MMU_BUSY=-882,
 I9100_MMU_IO=-883,I9100_MMU_FAULT=-884,I9100_MMU_VERSION=-885};
struct i9100_sysmmu_mapping {struct i9100_dma_buffer *memory;uint32_t offset,bytes;};
struct i9100_sysmmu_io {
 void *cookie;uint32_t (*read32)(void *,unsigned unit,unsigned off);
 void (*write32)(void *,unsigned unit,unsigned off,uint32_t);
 int (*lock)(void *);void (*unlock)(void *);
 int (*masters_idle)(void *);void (*barrier)(void *);
};
struct i9100_sysmmu {
 struct i9100_sysmmu_io io;struct i9100_dma_buffer *table;
 struct i9100_dma_buffer *mapped[16];uint32_t mapped_count;
 uint32_t initialized,enabled,attached,units,fault,saved[2][2],fault_status[2],fault_address[2];
};
int i9100_sysmmu_build(struct i9100_dma_buffer *table,const struct i9100_sysmmu_mapping *,unsigned count);
int i9100_sysmmu_init(struct i9100_sysmmu *,const struct i9100_sysmmu_io *,unsigned units);
int i9100_sysmmu_enable(struct i9100_sysmmu *,int);
int i9100_sysmmu_attach(struct i9100_sysmmu *,struct i9100_dma_buffer *,const struct i9100_sysmmu_mapping *,unsigned count,int private_controller_clock_lease_verified);
int i9100_sysmmu_detach(struct i9100_sysmmu *);
int i9100_sysmmu_fault(struct i9100_sysmmu *);
int i9100_sysmmu_idle(void *);
/* Private v1/v2 controllers only.32KiB DMA32 table,16KiB L1 +16 L2 tables.
 * Identity maps only complete owned4KiB pages; no whole-RAM section mapping.
 * Build/enable do no MMIO. Attach requires all masters stopped, preserves
 * prior disabled controller CFG/PTBASE and holds table ownership until both
 * controllers are proved disabled. Faults never unblock/map missing memory.
 * MFC's two controllers share one table lease, not two duplicate DMA owners. */
#ifdef __cplusplus
}
#endif
#endif
