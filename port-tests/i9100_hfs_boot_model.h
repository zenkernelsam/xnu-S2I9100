/* SPDX-License-Identifier: BSD-2-Clause */
/* Host vnode/allocator/registry/HFS-disk boundary ONLY. The checker extracts
 * actual mount_common and VFS_MOUNT, and includes unchanged production boot
 * helpers and the initial HFS argument path. This is not a full XNU VFS. */
#ifndef I9100_HFS_BOOT_MODEL_H
#define I9100_HFS_BOOT_MODEL_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/types.h>
#include <strings.h>
#include "i9100_hfs_kernel_args.h"
#define BOARD_CONFIG_EXYNOS4210_I9100 1
#define CONFIG_MACF 0
#define CONFIG_VFS_FUNNEL 0
#define NFSCLIENT 0
#define NAMEDSTREAMS 0
#define __unused __attribute__((unused))
#define __private_extern__
typedef uint64_t user_addr_t;typedef uint32_t user32_addr_t;
typedef int boolean_t;typedef unsigned u_int,u_int32_t;typedef char *caddr_t;
#define TRUE 1
#define FALSE 0
#define USER_ADDR_NULL 0
#define CAST_USER_ADDR_T(x) ((user_addr_t)(uintptr_t)(x))
#define CAST_DOWN(type,x) ((type)(uintptr_t)(x))
#define CAST_DOWN_EXPLICIT(type,x) ((type)(uintptr_t)(x))
#define makedev(a,b) ((dev_t)(((uint32_t)(a)<<24)|((uint32_t)(b)&0xffffffU)))
#define major(x) (((uint32_t)(x)>>24)&0xffU)
#define minor(x) ((uint32_t)(x)&0xffffffU)
#define MFSNAMELEN 16
#define MFSTYPENAMELEN 16
#define MAXPATHLEN 256
#define MAXPHYS (1024U*1024U)
#define DEV_BSIZE 512
#define PAGE_MASK 4095
#define CACHED_LOOKUP_RIGHT_TTL 10
#define MNT_DEFAULT_IOQUEUE_DEPTH 32
#define LOWPRI_MAX_NUM_DEV 16
#define KAUTH_UID_NONE ((uid_t)-1)
#define KAUTH_GID_NONE ((gid_t)-1)
#define KAUTH_VNODE_READ_DATA 1
#define KAUTH_VNODE_WRITE_DATA 2
#define MNT_RDONLY 0x1
#define MNT_SYNCHRONOUS 0x2
#define MNT_NOEXEC 0x4
#define MNT_NOSUID 0x8
#define MNT_NODEV 0x10
#define MNT_UNION 0x20
#define MNT_ASYNC 0x40
#define MNT_CPROTECT 0x80
#define MNT_QUARANTINE 0x400
#define MNT_ROOTFS 0x4000
#define MNT_DOVOLFS 0x8000
#define MNT_UPDATE 0x10000
#define MNT_RELOAD 0x40000
#define MNT_FORCE 0x80000
#define MNT_DONTBROWSE 0x100000
#define MNT_AUTOMOUNTED 0x400000
#define MNT_JOURNALED 0x800000
#define MNT_UNKNOWNPERMISSIONS 0x200000
#define MNT_DEFWRITE 0x2000000
#define MNT_MULTILABEL 0x4000000
#define MNT_NOATIME 0x10000000
#define MNT_VISFLAGMASK 0xffffffffU
#define MNTK_TYPENAME_OVERRIDE 0x800
#define MNTK_KERNEL_MOUNT 0x1000
#define MNTK_WANTRDWR 0x4000000
#define MNTK_EXTENDED_ATTRS 0x80000
#define MNTK_PATH_FROM_ID 0x10000
#define MNTK_UNMOUNT_PREFLIGHT 0x20000
#define MNT_LUNMOUNT 2
#define MNT_LDEAD 0x100
#define VFC_VFSLOCALARGS 2
#define VFC_VFSNATIVEXATTR 0x10
#define VFC_VFSPREFLIGHT 0x40
#define KERNEL_MOUNT_NOAUTH 1
#define FREAD 1
#define FWRITE 2
#define S_IFBLK 060000
#define MNT_WAIT 1
#define BUF_WRITE_DATA 1
#define LOOKUP 0
#define OP_MOUNT 1
#define FOLLOW 1
#define AUDITVNPATH1 2
#define WANTPARENT 4
#define UIO_SYSSPACE 2
#define UIO_USERSPACE 0
#define VNODE_LOOKUP_NOFOLLOW 1
#define VNODE_LOOKUP_NOCROSSMOUNT 2
#define VDIR 2
#define VBLK 3
#define VLNK 5
#define VROOT 1
#define VMOUNT 2
#define SI_MOUNTEDON 1
#define VQ_UPDATE 1
#define VFS_KERNEL_EVENT 1
#define VOL_CAPABILITIES_INTERFACES 0
#define VOL_CAPABILITIES_FORMAT 1
#define VOL_CAP_INT_EXTENDED_ATTR 1
#define VOL_CAP_FMT_PATH_FROM_ID 1
#define HFSFSMNT_EXTENDED_ARGS 4
#define I9100_ROOT_MISSING 2
#define HFS_MOUNT_DEBUG 1
#define E_NONE 0
#define NULLVP NULL
struct proc {int p_acflag;void *task;};typedef struct proc *proc_t;
struct vfs_context {unsigned uid,is64;};typedef struct vfs_context *vfs_context_t;
struct mount;typedef struct mount *mount_t;
struct vnode {unsigned v_type,v_flag,v_specflags;dev_t v_rdev;mount_t v_mount,v_mountedhere;unsigned iocount,usecount,opens;};
typedef struct vnode *vnode_t;
struct componentname {char *cn_pnbuf;size_t cn_pnlen;};
struct nameidata {vnode_t ni_vp,ni_dvp;struct componentname ni_cnd;int segment;user_addr_t path;};
struct hfs_mount_args {uint32_t hfs_uid,hfs_gid;uint16_t hfs_mask,reserved;uint32_t hfs_encoding;
 struct {int32_t tz_minuteswest,tz_dsttime;} hfs_timezone;int32_t flags,journal_tbuffer_size,journal_flags,journal_disable;};
struct hfsmount {void *jnl;};
struct vfsops {int (*vfs_mount)(mount_t,vnode_t,user_addr_t,vfs_context_t);};
struct vfstable {char vfc_name[16];unsigned vfc_vfsflags,vfc_refcount,vfc_flags;struct vfsops *vfc_vfsops;struct vfstable *vfc_next;};
struct vfsstatfs {char f_fstypename[16],f_mntonname[256],f_mntfromname[256];unsigned f_owner;};
struct mount {unsigned mnt_flag,mnt_kern_flag,mnt_lflag,mnt_crossref,mnt_rwlock;
 unsigned mnt_maxreadcnt,mnt_maxwritecnt,mnt_segreadcnt,mnt_segwritecnt,mnt_maxsegreadsize,mnt_maxsegwritesize;
 unsigned mnt_devblocksize,mnt_alignmentmask,mnt_ioqueue_depth,mnt_ioscale,mnt_ioflags,mnt_authcache_ttl,mnt_throttle_mask,mnt_devbsdunit;
 vnode_t mnt_realrootvp,mnt_vnodecovered,mnt_devvp;struct vfsops *mnt_op;struct vfstable *mnt_vtable;struct vfsstatfs mnt_vfsstat;
 struct {void *tqh_first;} mnt_vnodelist,mnt_workerqueue,mnt_newvnodes;struct hfsmount hfs;};
struct vnode_attr {uint64_t va_nchildren;int supported;};
struct vfs_attr {struct {uint32_t capabilities[2],valid[2];} f_capabilities;};
#define VATTR_INIT(x) memset((x),0,sizeof(*(x)))
#define VATTR_WANTED(x,f) ((void)(x))
#define VATTR_IS_SUPPORTED(x,f) ((x)->supported)
#define VFSATTR_INIT(x) memset((x),0,sizeof(*(x)))
#define VFSATTR_WANTED(x,f) ((void)(x))
#define VFSATTR_IS_SUPPORTED(x,f) 0
#define VFSTOHFS(mp) (&(mp)->hfs)
#define TAILQ_INIT(q) ((q)->tqh_first=NULL)
#define AUDIT_ARG(a,b) ((void)(b))
#define SET(v,b) ((v)|=(b))
#define CLR(v,b) ((v)&=~(b))
static struct proc process;
static struct proc init_process;static proc_t initproc=&init_process;
static struct vfs_context kernel_context={0,0},user_context={0,0};
static mount_t dead_mountp;static unsigned mounts;
static struct mount root_mount;
static struct vnode root_node,covered_node,parent_node,device_node,mounted_node;
static struct vfsops ops;
static struct vfstable fs;static struct vfstable *vfsconf=&fs;
static unsigned vfs_nummntops,mount_generation;static dev_t rootdev;
static unsigned nblkdev=32;
static struct {int (*d_open)(dev_t,int,int,proc_t);} bdevsw[32];
static struct {
 int enabled,aliases,irq,interrupts,journal_error,lookup_error,attr_error,prepare_error,open_error,fs_error,checkdirs_error,list_error;
 unsigned lookup_calls,namei_calls,copyins,fs_mounts,closed,maintenance,root_state,nameidones,mount_notifications;
 unsigned nonempty,supported,journal_missing,wrong_mount,stale_tickets;
 unsigned namei_error,namei_nonempty,namei_wrong_dir,nonempty_second_attr,attr_calls;
 unsigned shutdowns,suspends,unmounts,seals,work_enqueued,timeout_calls,thread_allocs;
 unsigned unmount_error,seal_error,suspend_error,thread_allocate_error;
 char root_name[128];uint64_t tickets[2];struct hfs_mount_args user_args;
} model;
static jmp_buf maintenance_escape;
static void model_reset(void);
static void model_mount_free(void *);
static vfs_context_t vfs_context_kernel(void){return &kernel_context;}
static proc_t vfs_context_proc(vfs_context_t ctx){(void)ctx;return &process;}
static unsigned vfs_context_ucred(vfs_context_t ctx){return ctx->uid;}
static unsigned kauth_cred_getuid(unsigned uid){return uid;}
static int vfs_context_is64bit(vfs_context_t ctx){return ctx->is64;}
static int ml_at_interrupt_context(void){return model.irq;}
static int ml_get_interrupts_enabled(void){return model.interrupts;}
static int i9100_emmc_alias_devices(uint32_t n[4],uint64_t t[2])
{if(!model.aliases)return -1;uint32_t actual[]={14,9,14,10};memcpy(n,actual,sizeof(actual));memcpy(t,model.tickets,sizeof(model.tickets));return 0;}
static int i9100_emmc_alias_seal_data(const uint32_t n[4],const uint64_t t[2])
{assert(model.unmounts==1&&!device_node.opens&&!covered_node.v_mountedhere);
 assert(n[0]==14&&n[1]==9&&n[2]==14&&n[3]==10&&t[0]==100&&t[1]==101);
 model.seals++;if(model.seal_error)return -1;return 0;}
static unsigned model_mount_refs;
static void mount_ref(mount_t mp,int locked){(void)mp;assert(!locked);model_mount_refs++;}
static int safedounmount(mount_t mp,int flags,vfs_context_t ctx)
{assert(flags==0&&ctx==vfs_context_kernel()&&model_mount_refs==1&&mounted_node.iocount==0);
 model_mount_refs--;model.unmounts++;
 if(model.unmount_error)return EBUSY;
 mp->hfs.jnl=NULL;assert(device_node.usecount==1&&device_node.opens==1);device_node.usecount--;device_node.opens--;
 assert(covered_node.usecount==1);covered_node.usecount--;covered_node.v_mountedhere=NULL;model_mount_free(mp);return 0;}
typedef void *thread_call_param_t;
struct thread_call {void (*function)(void *,void *);};typedef struct thread_call *thread_call_t;
static struct thread_call model_thread_call;static unsigned system_inshutdown,hz=100;
#define THREAD_CALL_PRIORITY_KERNEL 1
#define KERN_SUCCESS 0
static thread_call_t thread_call_allocate_with_priority(void (*f)(void *,void *),void *p,unsigned priority)
{assert(!p&&priority==THREAD_CALL_PRIORITY_KERNEL);model.thread_allocs++;if(model.thread_allocate_error)return NULL;
 model_thread_call.function=f;return &model_thread_call;}
static void thread_call_enter(thread_call_t call){assert(call==&model_thread_call);model.work_enqueued++;}
static void timeout(void (*f)(void *),void *p,int ticks){assert(f&&!p&&ticks==(int)hz);model.timeout_calls++;}
static proc_t current_proc(void){return &process;}
static proc_t proc_find(int pid){assert(pid==1);return initproc;}
static void proc_rele(proc_t p){assert(p==initproc);}
static void proc_shutdown(void){assert(system_inshutdown&&i9100_storage_reboot_ready==I9100_STORAGE_REBOOT_PREPARING);model.shutdowns++;}
static int task_suspend(void *task){assert(model.shutdowns==1&&task==init_process.task);model.suspends++;return model.suspend_error?-1:0;}
static int PE_parse_boot_argn(const char *key,void *out,int n)
{if(!strcmp(key,"i9100emmcdata")){assert(n==(int)sizeof(int));memcpy(out,&model.enabled,n);return 1;}
 if(!strcmp(key,"rd")){assert((size_t)n>strlen(model.root_name));strcpy(out,model.root_name);return 1;}return 0;}
static void i9100_usbcdc_set_root_state(unsigned state){model.root_state=state;}
static void IOSleep(unsigned ms){assert(ms==5000);model.maintenance++;longjmp(maintenance_escape,1);}
static int suser(unsigned uid,int *flag){(void)flag;return uid?EPERM:0;}
static void mount_list_lock(void){}static void mount_list_unlock(void){}
static void mount_lock_spin(mount_t m){(void)m;}static void mount_unlock(mount_t m){(void)m;}
static void vnode_lock_spin(vnode_t v){(void)v;}static void vnode_unlock(vnode_t v){(void)v;}
static void lck_rw_lock_exclusive(unsigned *l){assert(!*l);*l=1;}
static void lck_rw_done(unsigned *l){assert(*l);*l=0;}
static void mount_lock_init(mount_t m){(void)m;}static void mount_lock_destroy(mount_t m){assert(!m->mnt_rwlock);}
static void name_cache_lock(void){}static void name_cache_unlock(void){}
static int vfs_isrdonly(mount_t m){return (m->mnt_flag&MNT_RDONLY)!=0;}
static uint64_t vfs_flags(mount_t m){return m->mnt_flag;}
static int vfs_64bitready(mount_t m){(void)m;return 1;}
static struct vfsstatfs *vfs_statfs(mount_t m){return &m->mnt_vfsstat;}
static mount_t vnode_mount(vnode_t v){return v->v_mount;}
static unsigned vnode_vtype(vnode_t v){return v->v_type;}
static void vfs_setowner(mount_t m,uid_t uid,gid_t gid){(void)m;(void)uid;(void)gid;}
static int prepare_coveredvp(vnode_t v,vfs_context_t c,struct componentname *n,const char *f,boolean_t skip)
{(void)c;(void)n;assert(!strcmp(f,"hfs")&&!skip);if(model.prepare_error)return EACCES;assert(!v->v_mountedhere);SET(v->v_flag,VMOUNT);return 0;}
static void *model_mount_allocate(size_t n){mounts++;return calloc(1,n);}
static void model_mount_free(void *p){assert(mounts);mounts--;free(p);}
#define MALLOC_ZONE(p,type,size,a,b) ((p)=(type)model_mount_allocate(size))
#define FREE_ZONE(p,size,a) model_mount_free(p)
static int vnode_getattr(vnode_t v,struct vnode_attr *a,vfs_context_t c)
{(void)c;assert(v==&covered_node);model.attr_calls++;if(model.attr_error)return EIO;
 a->va_nchildren=model.nonempty+(model.nonempty_second_attr&&model.attr_calls>1);a->supported=model.supported;return 0;}
static int vnode_lookup(const char *p,int flags,vnode_t *v,vfs_context_t c)
{assert(c==vfs_context_kernel());model.lookup_calls++;if(model.lookup_error==(int)model.lookup_calls)return ENOENT;
 if(!strcmp(p,"/"))*v=&root_node;
 else if(!strcmp(p,"/dev/i9100-data"))*v=&device_node;
 else {assert(!strcmp(p,"/private/var"));*v=covered_node.v_mountedhere&&!(flags&VNODE_LOOKUP_NOCROSSMOUNT)?&mounted_node:&covered_node;}
 (*v)->iocount++;return 0;}
static void ndinit(struct nameidata *nd,int segment,user_addr_t path)
{memset(nd,0,sizeof(*nd));nd->segment=segment;nd->path=path;}
#define NDINIT(nd,a,b,c,s,p,ctx) ndinit((nd),(s),(p))
static int namei(struct nameidata *nd)
{model.namei_calls++;if(model.namei_error==model.namei_calls)return ENOENT;
 const char *path=nd->segment==UIO_USERSPACE?"/dev/i9100-data":(const char *)(uintptr_t)nd->path;
 if(!strcmp(path,"/private/var")){nd->ni_vp=&covered_node;nd->ni_dvp=&parent_node;covered_node.iocount++;parent_node.iocount++;}
 else {assert(!strcmp(path,"/dev/i9100-data"));nd->ni_vp=&device_node;device_node.iocount++;}
 if(model.namei_wrong_dir&&nd->ni_dvp)nd->ni_vp->v_type=VLNK;
 nd->ni_cnd.cn_pnbuf=(char *)path;nd->ni_cnd.cn_pnlen=strlen(path)+1;return 0;}
static void nameidone(struct nameidata *nd){(void)nd;model.nameidones++;}
static void vnode_put(vnode_t v){assert(v&&v->iocount);v->iocount--;}
static int vnode_ref(vnode_t v){v->usecount++;return 0;}
static void vnode_rele(vnode_t v){assert(v->usecount);v->usecount--;}
static void vnode_getalways(vnode_t v){v->iocount++;}
static unsigned vcount(vnode_t v){return v->usecount;}
static int vfs_mountedon(vnode_t v){return v->v_specflags&SI_MOUNTEDON?EBUSY:0;}
static int VNOP_FSYNC(vnode_t v,int w,vfs_context_t c){(void)v;(void)c;assert(w==MNT_WAIT);return 0;}
static int buf_invalidateblks(vnode_t v,int w,int a,int b){(void)v;(void)a;(void)b;assert(w==BUF_WRITE_DATA);return 0;}
static int VNOP_OPEN(vnode_t v,int access,vfs_context_t c)
{assert(v==&device_node&&access==(FREAD|FWRITE));(void)c;if(model.open_error)return EIO;v->opens++;return 0;}
static int VNOP_CLOSE(vnode_t v,int access,vfs_context_t c){assert(v==&device_node&&v->opens);(void)c;(void)access;v->opens--;model.closed++;return 0;}
static int vnode_authorize(vnode_t v,void *p,unsigned mode,vfs_context_t c){(void)v;(void)p;(void)mode;(void)c;return 0;}
static int checkdirs(vnode_t v,vfs_context_t c){(void)c;assert(v==&covered_node);return model.checkdirs_error?EIO:0;}
static int mount_list_add(mount_t m){(void)m;return model.list_error?EBUSY:0;}
static int vfs_getattr(mount_t m,struct vfs_attr *a,vfs_context_t c){(void)m;(void)a;(void)c;return ENOTSUP;}
static void OSAddAtomic(int n,unsigned *v){*v+=n;}
static void enablequotas(mount_t m,vfs_context_t c){(void)m;(void)c;}
static void vfs_init_io_attributes(vnode_t v,mount_t m){(void)v;(void)m;}
static void vfs_notify_mount(vnode_t v){assert(v==&parent_node);model.mount_notifications++;}
static void vfs_event_signal(void *p,int e,intptr_t x){(void)p;(void)e;(void)x;}
static int VFS_START(mount_t m,int f,vfs_context_t c){(void)m;(void)f;(void)c;return 0;}
static int VFS_UNMOUNT(mount_t m,int f,vfs_context_t c){(void)f;(void)c;m->hfs.jnl=NULL;return 0;}
static void mount_dropcrossref(mount_t m,vnode_t v,int n){(void)v;(void)n;model_mount_free(m);}
static void panic(const char *fmt,...){fprintf(stderr,"model kernel panic: %s\n",fmt);abort();}
static int copyin(user_addr_t address,void *dst,size_t bytes)
{model.copyins++;if(address==0x1100U){assert(bytes==4);uint32_t device=0x2200;memcpy(dst,&device,bytes);return 0;}
 if(address==0x1104U){assert(bytes==sizeof(model.user_args));memcpy(dst,&model.user_args,bytes);return 0;}return EFAULT;}
static int model_hfs_disk_mount(mount_t m,vnode_t dev,const struct hfs_mount_args *args)
{model.fs_mounts++;assert(args->hfs_mask==0755&&args->flags==4&&!args->journal_disable);
 if(model.fs_error)return EIO;
 if(!(m->mnt_flag&MNT_UPDATE)){assert(dev==&device_node&&dev->opens==1);m->hfs.jnl=model.journal_missing?NULL:(void *)1;
  m->mnt_flag|=MNT_JOURNALED;mounted_node.v_mount=model.wrong_mount?&root_mount:m;mounted_node.v_type=VDIR;mounted_node.v_flag=VROOT;}
 if(model.stale_tickets)model.tickets[1]++;return 0;}
static int hfs_mount(mount_t,vnode_t,user_addr_t,vfs_context_t);
static int VFS_MOUNT(mount_t,vnode_t,user_addr_t,vfs_context_t);
static int mount_common(char *,vnode_t,vnode_t,struct componentname *,user_addr_t,int,uint32_t,char *,boolean_t,vfs_context_t);
#endif
