/* SPDX-License-Identifier: BSD-2-Clause */
/* Exact private extension of mount_common, included in its source copy.
 * One early boot request; no new userspace syscall or generic kernel-copy
 * escape. The held actual DATAFS BSD identity must remain the same through
 * name lookup, VFS_MOUNT and journal verification. */
static const struct i9100_hfs_kernel_args *i9100_hfs_active_args;
static const struct i9100_hfs_kernel_request *i9100_hfs_active_request;
static mount_t i9100_hfs_active_mount;
static volatile uint32_t i9100_hfs_mount_once;

int i9100_hfs_kernel_request_validate(const struct i9100_hfs_kernel_request *r)
{
 uint32_t numbers[4];uint64_t tickets[2];
 if(!r||r->abi!=I9100_HFS_KERNEL_ABI||r->size!=sizeof(*r)||!r->device||
    strcmp(r->device,"/dev/i9100-data")||!i9100_hfs_kernel_args_valid(&r->args)||
    ml_at_interrupt_context()||!ml_get_interrupts_enabled()||
    i9100_emmc_alias_devices(numbers,tickets))return EPERM;
 if(r->system_major!=numbers[0]||r->system_minor!=numbers[1]||
    r->data_major!=numbers[2]||r->data_minor!=numbers[3]||
    r->system_ticket!=tickets[0]||r->data_ticket!=tickets[1]||
    rootdev!=makedev(numbers[0],numbers[1]))return EXDEV;
 return 0;
}
int i9100_hfs_kernel_mount_args(const struct mount *mp,const struct i9100_hfs_kernel_args *args)
{
 return !mp||mp!=i9100_hfs_active_mount||!args||args!=i9100_hfs_active_args||
  !i9100_hfs_active_request||i9100_hfs_kernel_request_validate(i9100_hfs_active_request)||
  !i9100_hfs_kernel_args_valid(args)||!mp->mnt_devvp||
  mp->mnt_devvp->v_rdev!=makedev(i9100_hfs_active_request->data_major,i9100_hfs_active_request->data_minor)||
  strcmp(mp->mnt_vfsstat.f_fstypename,"hfs")||
  strcmp(mp->mnt_vfsstat.f_mntonname,"/private/var")?EPERM:0;
}
static int i9100_hfs_private_mount(struct i9100_hfs_kernel_request *request)
{
 struct nameidata nd;vfs_context_t ctx=vfs_context_kernel();int error;
 if((error=i9100_hfs_kernel_request_validate(request)))return error;
 if(!__sync_bool_compare_and_swap(&i9100_hfs_mount_once,0U,1U))return EALREADY;
 NDINIT(&nd,LOOKUP,OP_MOUNT,FOLLOW|AUDITVNPATH1|WANTPARENT,
  UIO_SYSSPACE,CAST_USER_ADDR_T("/private/var"),ctx);
 if((error=namei(&nd)))return error;
 if(!nd.ni_vp||!nd.ni_dvp||nd.ni_vp->v_type!=VDIR||nd.ni_vp->v_mountedhere||
    nd.ni_vp->v_mount!=nd.ni_dvp->v_mount||!vfs_isrdonly(nd.ni_vp->v_mount)){
  if(nd.ni_vp)vnode_put(nd.ni_vp);if(nd.ni_dvp)vnode_put(nd.ni_dvp);nameidone(&nd);return EBUSY;
 }
 struct vnode_attr attr;VATTR_INIT(&attr);VATTR_WANTED(&attr,va_nchildren);
 error=vnode_getattr(nd.ni_vp,&attr,ctx);
 if(error||!VATTR_IS_SUPPORTED(&attr,va_nchildren)||attr.va_nchildren){
  if(!error)error=ENOTEMPTY;vnode_put(nd.ni_vp);vnode_put(nd.ni_dvp);nameidone(&nd);return error;
 }
 i9100_hfs_active_request=request;
 error=mount_common("hfs",nd.ni_dvp,nd.ni_vp,&nd.ni_cnd,
  CAST_USER_ADDR_T(request),MNT_NOSUID|MNT_NODEV,0,NULL,TRUE,ctx);
 i9100_hfs_active_request=NULL;i9100_hfs_active_args=NULL;i9100_hfs_active_mount=NULL;
 if(nd.ni_vp)vnode_put(nd.ni_vp);if(nd.ni_dvp)vnode_put(nd.ni_dvp);nameidone(&nd);
 return error;
}
