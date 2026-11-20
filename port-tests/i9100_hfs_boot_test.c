/* SPDX-License-Identifier: BSD-2-Clause */
/* Appended to extracted actual VFS/HFS prologue + boot implementation. */
static void model_reset(void)
{
 if(covered_node.v_mountedhere){mount_t mp=covered_node.v_mountedhere;assert(mounts==1);model_mount_free(mp);}
 assert(!mounts);memset(&model,0,sizeof(model));memset(&root_mount,0,sizeof(root_mount));
 memset(&root_node,0,sizeof(root_node));memset(&covered_node,0,sizeof(covered_node));
 memset(&parent_node,0,sizeof(parent_node));memset(&device_node,0,sizeof(device_node));memset(&mounted_node,0,sizeof(mounted_node));
 memset(&fs,0,sizeof(fs));memset(&ops,0,sizeof(ops));
 strcpy(fs.vfc_name,"hfs");fs.vfc_vfsflags=VFC_VFSLOCALARGS;fs.vfc_vfsops=&ops;ops.vfs_mount=hfs_mount;
 root_mount.mnt_flag=MNT_RDONLY|MNT_ROOTFS;strcpy(root_mount.mnt_vfsstat.f_fstypename,"hfs");
 root_node.v_type=VDIR;root_node.v_mount=&root_mount;root_node.v_flag=VROOT;
 covered_node.v_type=parent_node.v_type=VDIR;covered_node.v_mount=parent_node.v_mount=&root_mount;
 device_node.v_type=VBLK;device_node.v_rdev=makedev(14,10);model.supported=1;model.aliases=1;model.interrupts=1;
 rootdev=makedev(14,9);model.enabled=1;strcpy(model.root_name,"i9100-system");model.tickets[0]=100;model.tickets[1]=101;
 model.user_args.hfs_mask=0755;model.user_args.flags=4;
 i9100_hfs_mount_once=0;i9100_hfs_active_args=NULL;i9100_hfs_active_request=NULL;i9100_hfs_active_mount=NULL;
 i9100_hfs_data_enabled=0;memset((void *)i9100_hfs_data_diag,0,sizeof(i9100_hfs_data_diag));i9100_hfs_data_diag[0]=1;
 i9100_storage_reboot_gate=i9100_storage_reboot_ready=i9100_storage_reboot_request=0;
 memset(i9100_data_bound_numbers,0,sizeof(i9100_data_bound_numbers));memset(i9100_data_bound_tickets,0,sizeof(i9100_data_bound_tickets));
 i9100_storage_shutdown_call=NULL;system_inshutdown=model_mount_refs=0;
 memset((void *)i9100_storage_shutdown_diag,0,sizeof(i9100_storage_shutdown_diag));i9100_storage_shutdown_diag[0]=1;
 vfs_nummntops=mount_generation=0;model.mount_notifications=0;
}
static void quiet(void)
{assert(!root_node.iocount&&!covered_node.iocount&&!parent_node.iocount&&!device_node.iocount&&!mounted_node.iocount);
 assert(!i9100_hfs_active_request&&!i9100_hfs_active_args&&!i9100_hfs_active_mount);}
int main(void)
{
 alarm(20);unsigned cases=0;
 model_reset();model.enabled=0;strcpy(model.root_name,"disk0s1");i9100_hfs_data_bootstrap();assert(!model.lookup_calls&&!model.fs_mounts);cases++;
 for(unsigned failure=0;failure<25;failure++){
  model_reset();int expected=0;
  switch(failure){
   case 0:model.enabled=0;expected=EPERM;break;
   case 1:strcpy(model.root_name,"disk0s1");expected=EINVAL;break;
   case 2:model.aliases=0;expected=ENXIO;break;
   case 3:rootdev=makedev(14,11);expected=EXDEV;break;
   case 4:root_mount.mnt_flag&=~MNT_RDONLY;expected=EROFS;break;
   case 5:strcpy(root_mount.mnt_vfsstat.f_fstypename,"ext4");expected=EROFS;break;
   case 6:covered_node.v_type=VLNK;expected=EBUSY;break;
   case 7:model.nonempty=1;expected=ENOTEMPTY;break;
   case 8:model.supported=0;expected=ENOTEMPTY;break;
   case 9:model.attr_error=1;expected=EIO;break;
   case 10:device_node.v_rdev=makedev(14,8);expected=EXDEV;break;
   case 11:model.lookup_error=3;expected=ENOENT;break;
   case 12:model.prepare_error=1;expected=EACCES;break;
   case 13:model.open_error=1;expected=EIO;break;
   case 14:model.fs_error=1;expected=EIO;break;
   case 15:model.journal_missing=1;expected=EINVAL;break;
   case 16:model.stale_tickets=1;expected=EXDEV;break;
   case 17:model.namei_error=1;expected=ENOENT;break;
   case 18:model.namei_error=2;expected=ENOENT;break;
   case 19:model.nonempty_second_attr=1;expected=ENOTEMPTY;break;
   case 20:model.namei_wrong_dir=1;expected=EBUSY;break;
   case 21:model.checkdirs_error=1;expected=EIO;break;
   case 22:model.list_error=1;expected=EBUSY;break;
   case 23:model.wrong_mount=1;expected=EINVAL;break;
   case 24:model.thread_allocate_error=1;expected=ENOMEM;break;
  }
  if(!setjmp(maintenance_escape)){i9100_hfs_data_bootstrap();assert(!"failure must not start userland");}
  assert(model.maintenance==1&&model.root_state==I9100_ROOT_MISSING&&i9100_hfs_data_diag[1]==3&&
   i9100_hfs_data_diag[2]==(uint32_t)expected);quiet();
  assert(!model.copyins);if(!covered_node.v_mountedhere)assert(!mounts&&!device_node.usecount&&!device_node.opens);
  cases++;
 }
 model_reset();i9100_hfs_data_bootstrap();quiet();
 assert(i9100_hfs_data_diag[1]==2&&i9100_hfs_data_diag[7]==1&&model.fs_mounts==1&&!model.copyins&&mounts==1&&
  covered_node.v_mountedhere&&device_node.opens==1&&device_node.usecount==1&&vfs_nummntops==1&&model.mount_notifications==1);
 assert(!(covered_node.v_mountedhere->mnt_kern_flag&MNTK_KERNEL_MOUNT));cases++;
 assert(i9100_storage_reboot_gate==I9100_STORAGE_REBOOT_READY&&model.thread_allocs==1);
 /* Ordinary user remount still traverses original user32 fspec copyin and
  * original HFS body copyin after the scoped kernel flag has been cleared. */
 struct componentname cnp={(char *)"/private/var",13};
 assert(!mount_common("hfs",&parent_node,&mounted_node,&cnp,0x1100,MNT_UPDATE|MNT_NOSUID|MNT_NODEV,0,NULL,FALSE,&user_context));
 assert(model.copyins==2&&model.fs_mounts==2&&!i9100_hfs_active_args&&!i9100_hfs_active_mount);cases++;
 model_reset();struct i9100_hfs_kernel_request request={0};request.abi=1;request.size=sizeof(request);request.device="/dev/i9100-data";
 request.system_major=request.data_major=14;request.system_minor=9;request.data_minor=10;
 request.system_ticket=100;request.data_ticket=101;request.args.mask=0755;request.args.flags=4;
 assert(!i9100_hfs_kernel_request_validate(&request));
 request.args.journal_disable=1;assert(i9100_hfs_kernel_request_validate(&request)==EPERM);request.args.journal_disable=0;
 assert(mount_common("hfs",&parent_node,&covered_node,&cnp,CAST_USER_ADDR_T(&request),MNT_NOSUID|MNT_NODEV,0,NULL,TRUE,vfs_context_kernel())==EPERM);
 assert(!model.fs_mounts&&!mounts&&!fs.vfc_refcount);cases++;
 /* Native path rejects old creation lifetimes, wrong geometry identity,
  * untrusted kernel address/context and any request disabling journaling. */
 request.data_ticket++;assert(i9100_hfs_kernel_request_validate(&request)==EXDEV);request.data_ticket--;
 request.data_minor++;assert(i9100_hfs_kernel_request_validate(&request)==EXDEV);request.data_minor--;
 request.size--;assert(i9100_hfs_kernel_request_validate(&request)==EPERM);request.size++;
 model.irq=1;assert(i9100_hfs_kernel_request_validate(&request)==EPERM);model.irq=0;
 struct mount bogus={0};bogus.mnt_kern_flag=MNTK_KERNEL_MOUNT;
 assert(hfs_mount(&bogus,&device_node,CAST_USER_ADDR_T(&request.args),vfs_context_kernel())==EPERM&&!model.copyins);
 bogus.mnt_kern_flag=0;assert(hfs_mount(&bogus,&device_node,CAST_USER_ADDR_T(&request.args),vfs_context_kernel())==EFAULT&&model.copyins==1);
 cases++;
 /* Same production shutdown poll/worker: IRQ only enqueues, HFS unmount
  * must precede native seal, and every failure blocks readiness forever. */
 for(unsigned failure=0;failure<4;failure++){
  model_reset();i9100_hfs_data_bootstrap();assert(covered_node.v_mountedhere&&model.timeout_calls==1);
  if(failure==1)model.suspend_error=1;
  if(failure==2)model.unmount_error=1;
  if(failure==3)model.seal_error=1;
  i9100_storage_reboot_request=2;i9100_storage_shutdown_poll(NULL);
  assert(model.work_enqueued==1&&!model.shutdowns&&!model.unmounts&&!model.seals&&
   i9100_storage_reboot_ready==I9100_STORAGE_REBOOT_PREPARING);
  model_thread_call.function(NULL,NULL);
  assert(model.shutdowns==1&&model.suspends==1&&system_inshutdown);
  if(!failure){assert(i9100_storage_reboot_ready==I9100_STORAGE_REBOOT_READY&&model.unmounts==1&&model.seals==1&&
      !covered_node.v_mountedhere&&!mounts&&i9100_hfs_data_diag[1]==4);}
  else assert(i9100_storage_reboot_ready==I9100_STORAGE_REBOOT_FAILED&&i9100_storage_reboot_gate==I9100_STORAGE_REBOOT_FAILED&&
   i9100_storage_shutdown_diag[1]==7&&i9100_storage_shutdown_diag[2]&&
   model.unmounts==(failure>=2?1U:0U)&&model.seals==(failure==3?1U:0U));
  unsigned enqueued=model.work_enqueued;i9100_storage_shutdown_poll(NULL);assert(model.work_enqueued==enqueued);
  quiet();cases++;
 }
 printf("PASS actual mount_common/VFS_MOUNT/HFS argument path+prelaunchd bootstrap: %u scenarios, ROroot/emptyvar/exactBSDidentity/journal prerequisite, failure cleanup, no kernel copyin, original user remount; vnode/registry/diskHFS facades, no actual mount or persistence\n",cases);
 return 0;
}
