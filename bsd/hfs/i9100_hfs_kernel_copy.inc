/* SPDX-License-Identifier: BSD-2-Clause */
/* Included only in the existing hfs_mount source-copy. Normal user mounts
 * still execute their original copyin; only a scoped kernel VFS request
 * marked on this exact mount may copy its already validated kernel body. */
if (mp->mnt_kern_flag & MNTK_KERNEL_MOUNT) {
 if(context!=vfs_context_kernel()||
    i9100_hfs_kernel_mount_args(mp,CAST_DOWN(struct i9100_hfs_kernel_args *,data)))return EPERM;
 if(sizeof(args)!=sizeof(struct i9100_hfs_kernel_args))return EINVAL;
 bcopy(CAST_DOWN(void *,data),&args,sizeof(args));
} else {
 if((retval=copyin(data,(caddr_t)&args,sizeof(args))))return retval;
}
