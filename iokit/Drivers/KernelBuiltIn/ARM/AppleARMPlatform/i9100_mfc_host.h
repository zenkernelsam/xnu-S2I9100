/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MFC_HOST_H
#define I9100_MFC_HOST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct i9100_mfc_host_lease {uintptr_t self,parent;uint32_t generation,parent_generation,owned;};
int i9100_mfc_host_acquire(const struct i9100_mfc_host_lease *parent,struct i9100_mfc_host_lease *);
int i9100_mfc_host_check(const struct i9100_mfc_host_lease *);
int i9100_mfc_host_has_child(const struct i9100_mfc_host_lease *);
int i9100_mfc_host_begin_close(const struct i9100_mfc_host_lease *);
int i9100_mfc_host_release(struct i9100_mfc_host_lease *,int actual_controller_quiescent);
/* Shared exact owner for MFC + its two MMUs and local power/clock block.
 * A board supplier holds the root; its one engine borrows a child. Legacy
 * engines acquire a root, preventing concurrent board power transitions.
 * Parent begin_close atomically fences child acquisition before any power
 * restoration; a live child prevents that fence. Parent release refuses it.
 * No MMIO; callers prove idle before release, and retain exact lease memory
 * until successful return. Copied addresses/generations never gain ownership. */
#ifdef __cplusplus
}
#endif
#endif
