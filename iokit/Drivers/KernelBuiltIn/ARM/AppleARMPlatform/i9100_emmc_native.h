/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_NATIVE_H
#define I9100_EMMC_NATIVE_H
#include "i9100_emmc_writer.h"
#include "i9100_emmc_host.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_emmc_native;
struct i9100_emmc_inherited_profile {
 uint32_t board_data_reviewed,ciu_clock_hz,source_mux,source_divider,clksel;
 uint32_t gpk0_mux,gpk0_pull,gpk0_drive;
};
struct i9100_emmc_native_snapshot {
 struct i9100_emmc_writer_identity identity;
 struct i9100_emmc_partition system,data;
 struct i9100_hfs_volume system_hfs,data_hfs;
 uint32_t system_hfs_valid,data_hfs_valid,mode,phase,host_generation;
 uint32_t command,rint,status,cid[4],cache_control,writer_dirty,writer_poisoned,sealed;
 int error,cleanup_error;
};
enum i9100_emmc_native_mode {I9100_EMMC_N_INSPECT=0,I9100_EMMC_N_DATA_RW=1};
int i9100_emmc_native_open(int enabled,struct i9100_peripheral_native *,
 const struct i9100_emmc_inherited_profile *,enum i9100_emmc_native_mode,
 const struct i9100_emmc_writer_identity *approved_identity,struct i9100_emmc_native **);
int i9100_emmc_native_snapshot(struct i9100_emmc_native *,struct i9100_emmc_native_snapshot *);
int i9100_emmc_native_validate(struct i9100_emmc_native *);
int i9100_emmc_native_read(struct i9100_emmc_native *,uint64_t absolute_lba,uint32_t sectors,uint8_t *);
int i9100_emmc_native_write_data(struct i9100_emmc_native *,uint64_t absolute_lba,uint32_t sectors,const uint8_t *);
int i9100_emmc_native_synchronize(struct i9100_emmc_native *);
int i9100_emmc_native_seal_data(struct i9100_emmc_native *); /* final sync + deny any later data write */
int i9100_emmc_native_hold_get(struct i9100_emmc_native *,uintptr_t consumer);
int i9100_emmc_native_hold_put(struct i9100_emmc_native *,uintptr_t consumer);
int i9100_emmc_native_close(struct i9100_emmc_native **);
/* Default OFF; real IOKit MMIO and single host/card lifecycle. Adopt a
 * reviewed inherited CIU rate/pins/mux/divider/CLKSEL; verify live fields
 * on every IO. No GPIO/CMU/PMIC programming or invented frequency.
 * Inspect reads real current CID/GPT+PIT/HFS. DATA_RW requires separately
 * approved current CID/table identity and two exact HFSX/HFS+ roles; only
 * DATAFS is writable at this runtime API. No system deployment interface.
 * Consumer holds pin the maps/card until the storage service stops. One
 * exact set of at most four consumers; close requires no consumer, clean writer and actual
 * CMD/FIFO/data-idle. Errors retain context/lease, never re-probe or retry.
 * Owner serializes entry/hold-get against final close. No production PE
 * registration is added by this module; hardware use requires acceptance. */
#ifdef __cplusplus
}
#endif
#endif
