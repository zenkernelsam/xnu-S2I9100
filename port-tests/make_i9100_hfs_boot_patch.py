#!/usr/bin/env python3
"""Prepare narrow internal DATAFS kernel mount on copies; never modify live VFS/HFS/boot."""
import argparse
import difflib
import hashlib
import json
from i9100_offline_build import REPO,cap_cpu
cap_cpu()
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write-unapplied-patch',action='store_true',required=True);p.parse_args()
names=('bsd/vfs/vfs_syscalls.c','bsd/hfs/hfs_vfsops.c','bsd/kern/bsd_init.c','bsd/kern/kern_shutdown.c','pexpert/arm/i9100_usbcdc.c')
raw={name:(REPO/name).read_bytes() for name in names}
text={name:value.decode('utf8') for name,value in raw.items()}
def replace(name,before,after):
    if text[name].count(before)!=1:raise RuntimeError('Live mount anchor changed: '+name)
    text[name]=text[name].replace(before,after,1)
vfs=names[0]
anchor='#include <pexpert/pexpert.h>'
replace(vfs,anchor,anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <machine/machine_routines.h>
#include <hfs/hfs_mount.h>
#include <hfs/hfs.h>
#include "../../hfs/i9100_hfs_kernel_args.h"
#include "../../../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_emmc_alias.h"
#include "../../../pexpert/arm/i9100_root_wait.h"
extern void IOSleep(unsigned int);
#endif'''.replace('../../hfs/','../hfs/').replace('../../../iokit/','../../iokit/').replace('../../../pexpert/','../../pexpert/'))
# Copies preserve source-relative include paths by keeping their actual tree
# beneath out. Build also supplies original REPO/bsd and source include paths.
anchor='uint32_t mount_generation = 0;'
replace(vfs,anchor,anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include "i9100_hfs_mount_common.inc"
#endif''')
anchor='\tboolean_t have_usecount = FALSE;'
replace(vfs,anchor,anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    const struct i9100_hfs_kernel_request *i9100Request = NULL;
#endif''')
anchor='''\tif (kernelmount && (vfsp->vfc_vfsflags & VFC_VFSLOCALARGS)) {
\t\terror = EINVAL;  /* unsupported request */
\t\tgoto out1;
\t}'''
replace(vfs,anchor,'''\tif (kernelmount && (vfsp->vfc_vfsflags & VFC_VFSLOCALARGS)) {
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
        i9100Request = CAST_DOWN(struct i9100_hfs_kernel_request *,fsmountargs);
        if (ctx != vfs_context_kernel() || i9100Request != i9100_hfs_active_request ||
            strcmp(fstypename,"hfs") || strcmp(cnp->cn_pnbuf,"/private/var") ||
            flags != (MNT_NOSUID|MNT_NODEV) || internal_flags ||
            (error=i9100_hfs_kernel_request_validate(i9100Request))) {
            error=EPERM;goto out1;
        }
#else
        error=EINVAL;goto out1;
#endif
    }''')
anchor='\t\tif (vfs_context_is64bit(ctx)) {\n\t\t\tif ( (error = copyin(fsmountargs, (caddr_t)&devpath, sizeof(devpath))) )'
replace(vfs,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
        if (i9100Request) {
            devpath=CAST_USER_ADDR_T(i9100Request->device);
            fsmountargs=CAST_USER_ADDR_T(&i9100Request->args);
        } else
#endif
''' + anchor)
anchor='\t\t\tNDINIT(&nd, LOOKUP, OP_MOUNT, FOLLOW, UIO_USERSPACE, devpath, ctx);'
replace(vfs,anchor,'''            int deviceSpace=UIO_USERSPACE;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
            if (i9100Request) deviceSpace=UIO_SYSSPACE;
#endif
            NDINIT(&nd,LOOKUP,OP_MOUNT,FOLLOW,deviceSpace,devpath,ctx);''')
anchor='\t\t\tif (devvp->v_type != VBLK) {'
replace(vfs,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
            if (i9100Request && (devvp->v_rdev != makedev(i9100Request->data_major,i9100Request->data_minor) ||
                i9100_hfs_kernel_request_validate(i9100Request))) {error=EXDEV;goto out2;}
#endif
''' + anchor)
anchor='\terror = VFS_MOUNT(mp, device_vnode, fsmountargs, ctx);'
replace(vfs,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (i9100Request) {
        mp->mnt_kern_flag |= MNTK_KERNEL_MOUNT;
        i9100_hfs_active_args=&i9100Request->args;i9100_hfs_active_mount=mp;
    }
#endif
''' + anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (i9100Request) {
        mp->mnt_kern_flag &= ~MNTK_KERNEL_MOUNT;
        i9100_hfs_active_args=NULL;i9100_hfs_active_mount=NULL;
    }
#endif''')
# Append bootstrap after the whole mount_common, retaining the original
# placement, accounting, preflight, failure cleanup and mount notifications.
anchor='/* '+ '\n * Flush in-core data, check for competing mount attempts,'
replace(vfs,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include "i9100_hfs_data_boot.inc"
#endif

''' + anchor)
hfs=names[1]
anchor='#include <sys/mount_internal.h>'
replace(hfs,anchor,anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include "i9100_hfs_kernel_args.h"
#endif''')
anchor='''\tif ((retval = copyin(data, (caddr_t)&args, sizeof(args)))) {
\t\tif (HFS_MOUNT_DEBUG) {
\t\t\tprintf("hfs_mount: copyin returned %d for fs\\n", retval);
\t\t}
\t\treturn (retval);
\t}'''
replace(hfs,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include "i9100_hfs_kernel_copy.inc"
#else
''' + anchor + '\n#endif')
boot=names[2]
anchor='\t/* Initialize signal state for process 0. */'
replace(boot,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    extern void i9100_hfs_data_bootstrap(void);
    i9100_hfs_data_bootstrap();
#endif

''' + anchor)
shutdown=names[3]
anchor='#include <kern/clock.h>\t\t\t/* for delay_for_interval() */'
replace(shutdown,anchor,anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <kern/thread_call.h>
#include "../hfs/i9100_hfs_kernel_args.h"
#endif
''')
anchor='static void proc_shutdown(void);'
replace(shutdown,anchor,anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include "i9100_hfs_shutdown.inc"
#endif''')
# Also route ordinary reboot/poweroff through the non-forced data prerequisite,
# since existing boot() ignores errors in global forced vfs_unmountall().
anchor='\tif (!OSCompareAndSwap(0, 1, &system_inshutdown)) {'
replace(shutdown,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (i9100_hfs_root_required() && (howto & (RB_QUICK|RB_NOSYNC)))return EPERM;
#endif
''' + anchor)
anchor='\t\tvfs_unmountall();'
replace(shutdown,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
        if (i9100_hfs_root_required()) {
            int data_error=i9100_hfs_data_shutdown();
            if (data_error) {
                printf("i9100 DATAFS boot shutdown errno=%d; reset blocked\\n",data_error);
                return data_error;
            }
        }
#endif
''' + anchor)
usb=names[4]
anchor='static int uctl_reset_action;'
replace(usb,anchor,anchor+'''
#ifdef I9100_USB_SIM
static volatile unsigned i9100_storage_reboot_gate,i9100_storage_reboot_ready,i9100_storage_reboot_request;
#else
extern volatile unsigned i9100_storage_reboot_gate,i9100_storage_reboot_ready,i9100_storage_reboot_request;
#endif''')
anchor='\t\t\tuctl_action = !strcmp(verb, "reboot-download") ? 2 : 1;'
replace(usb,anchor,'''            if (i9100_storage_reboot_gate && i9100_storage_reboot_gate != 2 && !i9100_usb_panic_mode) {
                answer="ERR storage-not-ready";
            } else {
''' + anchor)
anchor='\t\t\tanswer = uctl_action == 2 ? "OK reboot-download" : "OK reboot";'
replace(usb,anchor,anchor+'\n            }')
anchor='    if (!action || UCTL_NOW() < uctl_reset_at) return;'
replace(usb,anchor,anchor+'''
    if ((action==1 || action==2 || action==4) && i9100_storage_reboot_gate && !i9100_usb_panic_mode) {
        if (i9100_storage_reboot_gate != 2) {uctl_reset_action=0;return;}
        if (!i9100_storage_reboot_request)i9100_storage_reboot_request=(unsigned)action;
        if (i9100_storage_reboot_request != (unsigned)action || i9100_storage_reboot_ready==3) {
            uctl_reset_action=0;return;
        }
        if (i9100_storage_reboot_ready!=2)return;
    }''')
# The original Power worker understands only request1.
# USB action4 will remain disallowed for internal-root until this separate
# poweroff destination is connected; do not create a silent no-op command.
anchor='            if(!uctl_enabled || !i9100_lpm_poweroff_ready)answer="ERR disabled";'
replace(usb,anchor,'            if(!uctl_enabled || !i9100_lpm_poweroff_ready || i9100_storage_reboot_gate)answer="ERR disabled";')
anchor='extern volatile unsigned i9100_lpm_poweroff_ready,i9100_lpm_poweroff_request;'
replace(usb,anchor,'''#ifdef I9100_HFS_FROZEN_REFERENCE
/* Historical 11/16 link intentionally has no LPM Power worker. Keep it
 * disabled for this offline link, without importing the live charger's
 * different structures or masking a missing provider in production. */
static volatile unsigned i9100_lpm_poweroff_ready,i9100_lpm_poweroff_request;
#else
''' + anchor + '\n#endif')
patch=''.join(''.join(difflib.unified_diff(raw[name].decode().splitlines(True),text[name].splitlines(True),
    fromfile='a/'+name,tofile='b/'+name)) for name in names)
sources={name:hashlib.sha256(value).hexdigest() for name,value in raw.items()}
companions=('bsd/hfs/i9100_hfs_kernel_args.h','bsd/hfs/i9100_hfs_kernel_copy.inc',
    'bsd/vfs/i9100_hfs_mount_common.inc','bsd/vfs/i9100_hfs_data_boot.inc','bsd/kern/i9100_hfs_shutdown.inc')
meta=dict(sources=sources,companions={name:hashlib.sha256((REPO/name).read_bytes()).hexdigest() for name in companions},
    patch_sha256=hashlib.sha256(patch.encode()).hexdigest(),applied=False,hardware_enabled=False,
    paired_objects=['bsd/vfs_syscalls.o','bsd/hfs_vfsops.o','bsd/bsd_init.o','bsd/kern_shutdown.o','pexpert/i9100_usbcdc.o'],
    requirements='Explicit reviewed native pair; rd=i9100-system+i9100emmcdata=1; mandatory pre-launchd HFS+journal, checked worker unmount/native seal before reset; all five BSD/USB objects mandatory; hardware deployment still unapproved')
target=REPO/'port-tests/i9100_hfs_boot_integration.patch'
target.write_text(patch,encoding='utf8');target.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf8')
if any((REPO/name).read_bytes()!=value for name,value in raw.items()):raise RuntimeError('Live source drift; new patch stale')
print('PREPARED unapplied exact VFS/HFS/earlyboot integration; live files unchanged')
