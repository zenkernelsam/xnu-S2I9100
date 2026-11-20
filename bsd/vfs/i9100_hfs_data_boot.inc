/* SPDX-License-Identifier: BSD-2-Clause */
/* Executes after devfs and BEFORE bsd_utaskbootstrap/launchd. Neither fstab
 * nor a user daemon is assumed to have mounted var for us. Failures leave
 * the kernel in maintenance; they never enable a same-root writable var. */
volatile uint32_t i9100_hfs_data_diag[8]={1U};
static volatile uint32_t i9100_hfs_data_enabled;
volatile uint32_t i9100_storage_reboot_gate;
volatile uint32_t i9100_storage_reboot_ready;
volatile uint32_t i9100_storage_reboot_request;
static uint32_t i9100_data_bound_numbers[4];
static uint64_t i9100_data_bound_tickets[2];
int i9100_hfs_root_required(void)
{
 char rd[128]={0};
 if(!PE_parse_boot_argn("rd",rd,sizeof(rd)))(void)PE_parse_boot_argn("rootdev",rd,sizeof(rd));
 return !strcmp(rd[0]=='*'?rd+1:rd,"i9100-system");
}
static void i9100_hfs_data_maintenance(int error)
{
 i9100_hfs_data_diag[2]=(uint32_t)error;i9100_hfs_data_diag[1]=3U;
 i9100_usbcdc_set_root_state(I9100_ROOT_MISSING);
 printf("i9100 DATAFS: maintenance errno=%d; launchd blocked, no var fallback\n",error);
 for(;;)IOSleep(5000);
}
void i9100_hfs_data_bootstrap(void)
{
 char requested_root[128]={0};int enabled=0;uint32_t numbers[4];uint64_t tickets[2];
 vfs_context_t ctx=vfs_context_kernel();vnode_t root=NULLVP,covered=NULLVP,device=NULLVP,mounted=NULLVP;
 struct vnode_attr attr;struct i9100_hfs_kernel_request request;int error=0;
 (void)PE_parse_boot_argn("i9100emmcdata",&enabled,sizeof(enabled));
 if(!PE_parse_boot_argn("rd",requested_root,sizeof(requested_root)))
  (void)PE_parse_boot_argn("rootdev",requested_root,sizeof(requested_root));
 const char *name=requested_root[0]=='*'?requested_root+1:requested_root;
 if(strcmp(name,"i9100-system")){if(enabled)i9100_hfs_data_maintenance(EINVAL);return;}
 if(enabled!=1)i9100_hfs_data_maintenance(EPERM);
 i9100_storage_reboot_gate=I9100_STORAGE_REBOOT_PREPARING;
 i9100_hfs_data_enabled=1;i9100_hfs_data_diag[1]=1;
 if(ml_at_interrupt_context()||!ml_get_interrupts_enabled()||ctx!=vfs_context_kernel()||
    i9100_emmc_alias_devices(numbers,tickets)){error=ENXIO;goto out;}
 if(rootdev!=makedev(numbers[0],numbers[1])){error=EXDEV;goto out;}
 if((error=vnode_lookup("/",VNODE_LOOKUP_NOFOLLOW,&root,ctx)))goto out;
 if(!vfs_isrdonly(vnode_mount(root))||strcmp(vfs_statfs(vnode_mount(root))->f_fstypename,"hfs")){
  error=EROFS;goto out;
 }
 if((error=vnode_lookup("/private/var",VNODE_LOOKUP_NOFOLLOW|VNODE_LOOKUP_NOCROSSMOUNT,&covered,ctx)))goto out;
 if(vnode_vtype(covered)!=VDIR||vnode_mount(covered)!=vnode_mount(root)||covered->v_mountedhere){error=EBUSY;goto out;}
 VATTR_INIT(&attr);VATTR_WANTED(&attr,va_nchildren);
 if((error=vnode_getattr(covered,&attr,ctx)))goto out;
 if(!VATTR_IS_SUPPORTED(&attr,va_nchildren)||attr.va_nchildren){error=ENOTEMPTY;goto out;}
 if((error=vnode_lookup("/dev/i9100-data",VNODE_LOOKUP_NOFOLLOW,&device,ctx)))goto out;
 if(vnode_vtype(device)!=VBLK||device->v_rdev!=makedev(numbers[2],numbers[3])){error=EXDEV;goto out;}
 bzero(&request,sizeof(request));request.abi=I9100_HFS_KERNEL_ABI;request.size=sizeof(request);
 request.device="/dev/i9100-data";request.system_major=numbers[0];request.system_minor=numbers[1];
 request.data_major=numbers[2];request.data_minor=numbers[3];request.system_ticket=tickets[0];request.data_ticket=tickets[1];
 request.args.mask=0755;request.args.flags=HFSFSMNT_EXTENDED_ARGS;
 if((error=i9100_hfs_private_mount(&request)))goto out;
 if((error=vnode_lookup("/private/var",VNODE_LOOKUP_NOFOLLOW,&mounted,ctx)))goto out;
 mount_t mp=vnode_mount(mounted);
 if(mp==vnode_mount(root)||!mp->mnt_devvp||mp->mnt_devvp->v_rdev!=device->v_rdev||
    strcmp(mp->mnt_vfsstat.f_mntonname,"/private/var")||vfs_isrdonly(mp)||
    !(vfs_flags(mp)&MNT_JOURNALED)||!VFSTOHFS(mp)||!VFSTOHFS(mp)->jnl){error=EINVAL;goto out;}
 if((error=i9100_hfs_kernel_request_validate(&request)))goto out;
 memcpy(i9100_data_bound_numbers,numbers,sizeof(numbers));memcpy(i9100_data_bound_tickets,tickets,sizeof(tickets));
 i9100_hfs_data_diag[3]=numbers[0];i9100_hfs_data_diag[4]=numbers[1];
 i9100_hfs_data_diag[5]=numbers[2];i9100_hfs_data_diag[6]=numbers[3];
 i9100_hfs_data_diag[7]=1;i9100_hfs_data_diag[1]=2;
 printf("i9100 DATAFS: mounted /private/var journaled RW before launchd; root remains RO\n");
out:
 if(mounted)vnode_put(mounted);if(device)vnode_put(device);if(covered)vnode_put(covered);if(root)vnode_put(root);
 if(error)i9100_hfs_data_maintenance(error);
 i9100_hfs_shutdown_prepare(); /* process-context allocation before userland */
 if(i9100_storage_reboot_gate!=I9100_STORAGE_REBOOT_READY)i9100_hfs_data_maintenance(ENOMEM);
}
int i9100_hfs_data_shutdown(void)
{
 vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULLVP;mount_t mp;int error;
 if(!i9100_hfs_data_enabled||i9100_hfs_data_diag[1]!=2||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return EPERM;
 if((error=vnode_lookup("/private/var",VNODE_LOOKUP_NOFOLLOW,&vp,ctx)))return error;
 mp=vnode_mount(vp);
 if(!mp||mp->mnt_flag&MNT_ROOTFS||!mp->mnt_devvp||
    mp->mnt_devvp->v_rdev!=makedev(i9100_data_bound_numbers[2],i9100_data_bound_numbers[3])||
    strcmp(mp->mnt_vfsstat.f_fstypename,"hfs")||strcmp(mp->mnt_vfsstat.f_mntonname,"/private/var")){
  vnode_put(vp);return EXDEV;
 }
 /* Identical ref ownership to unmount(2). Drop the root vnode BEFORE
  * dounmount, otherwise our own iocount prevents its drain. No force. */
 mount_ref(mp,0);vnode_put(vp);
 error=safedounmount(mp,0,ctx);if(error)return error;
 /* HFS close drained journal and data writers. Seal native under its real
  * host lock, flush actual card, and forbid subsequent direct data writes. */
 error=i9100_emmc_alias_seal_data(i9100_data_bound_numbers,i9100_data_bound_tickets);
 if(error)return EIO;i9100_hfs_data_diag[1]=4U;return 0;
}
