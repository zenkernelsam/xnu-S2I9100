/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_HFS_KERNEL_ARGS_H
#define I9100_HFS_KERNEL_ARGS_H
#include <stdint.h>
/* Canonical ARM32 HFS syscall argument body, WITHOUT the preceding fspec
 * pointer removed by mount_common. Pointer-free and exact 40 byte ABI. */
struct i9100_hfs_kernel_args {
 uint32_t uid,gid;uint16_t mask,reserved;uint32_t encoding;
 int32_t minutes_west,dst,flags,journal_buffer,journal_flags,journal_disable;
};
struct i9100_hfs_kernel_request {
 uint32_t abi,size;const char *device;
 uint32_t system_major,system_minor,data_major,data_minor;
 uint64_t system_ticket,data_ticket;
 struct i9100_hfs_kernel_args args;
};
#define I9100_HFS_KERNEL_ABI 1U
static inline int i9100_hfs_kernel_args_valid(const struct i9100_hfs_kernel_args *a)
{
 /* HFS+ keeps its original per-file ownership/mode. These fallback fields
  * only affect obsolete HFS Standard, which native verification rejects. */
 return a&&a->uid==0&&a->gid==0&&a->mask==0755&&a->reserved==0&&a->encoding==0&&
  a->minutes_west==0&&a->dst==0&&a->flags==4&&a->journal_buffer==0&&
  a->journal_flags==0&&a->journal_disable==0;
}
/* Defined in a VFS source-copy integration. Not exported to userspace. */
struct mount;
int i9100_hfs_kernel_request_validate(const struct i9100_hfs_kernel_request *);
int i9100_hfs_kernel_mount_args(const struct mount *,const struct i9100_hfs_kernel_args *);
#define I9100_STORAGE_REBOOT_OFF 0U
#define I9100_STORAGE_REBOOT_PREPARING 1U
#define I9100_STORAGE_REBOOT_READY 2U
#define I9100_STORAGE_REBOOT_FAILED 3U
extern volatile uint32_t i9100_storage_reboot_gate;
extern volatile uint32_t i9100_storage_reboot_ready;
extern volatile uint32_t i9100_storage_reboot_request;
void i9100_hfs_shutdown_prepare(void);
int i9100_hfs_data_shutdown(void);
int i9100_hfs_root_required(void);
#endif
