/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_NATIVE_H
#define I9100_MFC_NATIVE_H
#include "i9100_mfc.h"
#include "i9100_peripheral_native.h"
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_mfc_native;
struct i9100_mfc_supply_lease {uintptr_t self,provider;uint32_t generation,owned;};
struct i9100_mfc_supply {
 void *cookie;
 int (*acquire)(void *,struct i9100_mfc_supply_lease *);
 int (*validate)(void *,const struct i9100_mfc_supply_lease *);
 int (*release)(void *,struct i9100_mfc_supply_lease *);
};
enum i9100_mfc_native_phase {I9100_MFC_N_BUILDING=1,I9100_MFC_N_PREPARED=2,
 I9100_MFC_N_BOOT=3,I9100_MFC_N_SYSINIT=4,I9100_MFC_N_OPEN=5,I9100_MFC_N_HEADER=6,
 I9100_MFC_N_READY=7,I9100_MFC_N_CLOSING=8,I9100_MFC_N_FAILED=9};
struct i9100_mfc_native_status {
 uint32_t phase,generation,pending,expected,last_reply,raw_interrupt,firmware_version,system_memory_bytes;
 uint32_t instance,instance_open,width,height,dpb_count,consumed_bytes,bank_a_state,bank_b_state,held_mask;
 int error,cleanup_error;
};
int i9100_mfc_native_open(int enabled,struct i9100_peripheral_native *,const struct i9100_mfc_supply *,struct i9100_mfc_native **);
int i9100_mfc_native_start(struct i9100_mfc_native *,const struct i9100_mfc_firmware_profile *,const void *,uint32_t,
                         const void *annexb_stream,uint32_t stream_bytes);
int i9100_mfc_native_poll(struct i9100_mfc_native *);
int i9100_mfc_native_status(struct i9100_mfc_native *,struct i9100_mfc_native_status *);
int i9100_mfc_native_close(struct i9100_mfc_native **);
/* Default OFF, MFCv5.1 only: one reservation, actual MMIO, private aligned
 * bankA8MiB/bankB1MiB, captured kernel crypto provider, explicit firmware
 * profile and real FW/SYSINIT/OPEN/HEADER responses. Header parsing cannot
 * produce a decoded image; DPB layout/codec frame submit are separate work.
 * Supply provider must own actual PMU MFC/gates/source/identity and both
 * SYSMMU resources. This first path requires both MMUs disabled and does
 * not claim an IOMMU-isolated mapping. Production supply is not registered
 * yet: callback constants and fixture provenance cannot authorize it.
 * Caller serializes API entry against final close; failures retain all
 * required allocations/maps/provider until safe explicit shutdown. */
#ifdef __cplusplus
}
#endif
#endif
