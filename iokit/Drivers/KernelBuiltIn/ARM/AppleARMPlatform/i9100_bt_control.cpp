/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bt_control.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSObject.h>
#include <machine/machine_routines.h>
#include <sys/kern_control.h>
#include <sys/proc.h>
#include <sys/kauth.h>
#include <sys/errno.h>
#include <string.h>
struct i9100_bt_control : public OSObject {
 OSDeclareDefaultStructors(i9100_bt_control)
public:
 IOLock *lock; i9100_bt_worker *worker;kern_ctl_ref reference;
 struct i9100_bt_control_status state;uint32_t borrowed,connecting,closing_token,published,consumer_bound;
 bool initialize(){lock=0;worker=0;reference=0;borrowed=connecting=closing_token=published=consumer_bound=0;memset(&state,0,sizeof(state));return OSObject::init();}
 virtual void free(){if(lock)IOLockFree(lock);OSObject::free();}
};
static bool overlaps(const void *a,size_t n,const void *b,size_t m)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>~(uintptr_t)0-n||y>~(uintptr_t)0-m||(x+n>y&&y+m>x);}
OSDefineMetaClassAndStructors(i9100_bt_control,OSObject)
/* The kernel-control name is unique system-wide. Atomic publication also
 * keeps a registration conflict from replacing the existing callback owner. */
static i9100_bt_control *singleton;
static volatile uint32_t registry_readers;
static bool swap(i9100_bt_control *expected,i9100_bt_control *replacement)
{return __sync_bool_compare_and_swap(&singleton,expected,replacement);}
static i9100_bt_control *pin()
{
 /* A reader announces before loading; an unpublishing owner retains its
  * creation reference until every preexisting reader has taken its reference. */
 (void)__sync_add_and_fetch(&registry_readers,1U);
 auto *c=__sync_val_compare_and_swap(&singleton,(i9100_bt_control *)0,(i9100_bt_control *)0);
 if(c)c->retain();(void)__sync_sub_and_fetch(&registry_readers,1U);return c;
}
static int mapped(int e)
{
 if(!e)return 0;if(e==I9100_BTS_AGAIN||e==I9100_BA_CREDIT)return EWOULDBLOCK;
 if(e==I9100_BTS_BUSY||e==I9100_BT_BOOT_BUSY||e==I9100_BT_BUSY)return EBUSY;
 if(e==I9100_BTS_OWNER||e==I9100_BA_OWNER)return EPERM;
 if(e==I9100_BTS_ARGUMENT||e==I9100_BT_BOOT_ARGUMENT)return EINVAL;
 if(e==I9100_BTS_UNSUPPORTED)return ENOTSUP;return EIO;
}
static bool authorized(uint32_t *pid,uint64_t *unique)
{
 static const uint8_t original_uuid[16]={0xaf,0x01,0x50,0xd5,0x08,0x5e,0x32,0x65,0x98,0xed,0xff,0x07,0x2a,0x23,0xd1,0x46};
 char name[32]={};proc_selfname(name,sizeof(name));
 if(kauth_cred_getuid(kauth_cred_get())!=501U||strcmp(name,"BTServer"))return false;
 proc_t process=proc_self();if(!process)return false;uint8_t uuid[16]={};
 proc_getexecutableuuid(process,uuid,sizeof(uuid));(void)proc_rele(process);
 if(memcmp(uuid,original_uuid,sizeof(uuid)))return false;
 *pid=(uint32_t)proc_selfpid();*unique=proc_selfuniqueid();return *unique&&*pid&&*pid<=0x7fffffffU;
}
static i9100_bt_control *current(kern_ctl_ref ref,void *token,uint32_t unit,int process_required)
{
 uint32_t pid=0;uint64_t unique=0;if(process_required&&!authorized(&pid,&unique))return 0;
 auto *c=pin();if(!c)return 0;if(c!=token){c->release();return 0;}
 IOLockLock(c->lock);
 bool ok=c->state.registered&&!c->state.closing&&c->reference==ref&&c->state.client_active&&c->state.client_unit==unit&&
         (!process_required||(pid==c->state.client_pid&&unique==c->state.client_uniqueid));
 if(ok)c->borrowed++;IOLockUnlock(c->lock);if(!ok)c->release();return ok?c:0;
}
static void callback_done(i9100_bt_control *c)
{IOLockLock(c->lock);c->borrowed--;IOLockUnlock(c->lock);c->release();}
static void consumer_retain(void *cookie){((i9100_bt_control *)cookie)->retain();}
static void consumer_release(void *cookie){((i9100_bt_control *)cookie)->release();}
static int received(void *cookie,const uint8_t *bytes,unsigned count)
{
 auto *c=(i9100_bt_control *)cookie;IOLockLock(c->lock);
 if(c->connecting&&!c->state.closing){IOLockUnlock(c->lock);return I9100_BTS_AGAIN;}
 if(!c->state.registered||c->state.closing||!c->state.client_active){IOLockUnlock(c->lock);return I9100_BTS_IO;}
 kern_ctl_ref ref=c->reference;uint32_t unit=c->state.client_unit;c->borrowed++;
 IOLockUnlock(c->lock);
 errno_t e=ctl_enqueuedata(ref,unit,(void *)bytes,count,0);
 IOLockLock(c->lock);c->borrowed--;
 if(e==ENOBUFS){if(c->state.rx_backpressure!=~(uint64_t)0)c->state.rx_backpressure++;}
 else if(!e)c->state.rx_enqueued=~(uint64_t)0-c->state.rx_enqueued<count?~(uint64_t)0:c->state.rx_enqueued+count;
 else if(!c->state.error)c->state.error=e;
 IOLockUnlock(c->lock);return e==ENOBUFS?I9100_BTS_AGAIN:e?I9100_BTS_IO:0;
}
static errno_t connected(kern_ctl_ref ref,struct sockaddr_ctl *address,void **token)
{
 if(!token||!address)return EINVAL;*token=0;
 uint32_t pid=0;uint64_t unique=0;if(!authorized(&pid,&unique))return EPERM;
 if(address->sc_len!=sizeof(*address)||address->sc_family!=32U||address->ss_sysaddr!=2U||!address->sc_unit)return EINVAL;
 for(unsigned n=0;n<5;n++)if(address->sc_reserved[n])return EINVAL;
 auto *c=pin();if(!c)return ENXIO;
 IOLockLock(c->lock);
 if(c->state.attempted_connects!=~(uint32_t)0)c->state.attempted_connects++;
 if(!c->state.registered||c->state.closing||c->reference!=ref||c->state.client_active||c->connecting||c->state.accepted_connects){IOLockUnlock(c->lock);c->release();return EBUSY;}
 c->connecting=1;IOLockUnlock(c->lock);
 int e=i9100_bt_worker_stream_open(c->worker,c);
 IOLockLock(c->lock);c->connecting=0;
 if(!e){c->state.client_active=1;c->state.client_pid=(uint32_t)pid;c->state.client_uniqueid=unique;c->state.client_unit=address->sc_unit;
  c->state.accepted_connects++;*token=c;}
 else if(!c->state.error)c->state.error=e;
 IOLockUnlock(c->lock);c->release();return mapped(e);
}
static errno_t disconnected(kern_ctl_ref ref,uint32_t unit,void *token)
{
 if(!token)return 0;auto *c=pin();if(!c)return EINVAL;if(c!=token){c->release();return EINVAL;}
 IOLockLock(c->lock);
 if(c->reference!=ref||c->state.client_unit!=unit||!c->state.client_active){IOLockUnlock(c->lock);c->release();return EINVAL;}
 c->state.closing=1;c->state.client_active=0;c->closing_token=1;c->borrowed++;IOLockUnlock(c->lock);
 int e=i9100_bt_worker_stream_close(c->worker,c);
 IOLockLock(c->lock);if(!e)c->closing_token=0;else c->state.cleanup_error=e;
 IOLockUnlock(c->lock);callback_done(c);return mapped(e);
}
static errno_t sent(kern_ctl_ref ref,uint32_t unit,void *token,mbuf_t data,int)
{
 if(!data)return EINVAL;auto *c=current(ref,token,unit,1);if(!c){mbuf_freem(data);return EPERM;}
 size_t count=0;unsigned nodes=0;
 for(mbuf_t next=data;next;next=mbuf_next(next)){
  size_t n=mbuf_len(next);if(++nodes>256U||n>I9100_BT_STREAM_CAP-count){count=I9100_BT_STREAM_CAP+1U;break;}count+=n;}
 int e=EMSGSIZE;uint8_t *bytes=0;
 if(count&&count<=I9100_BT_STREAM_CAP){
  bytes=(uint8_t *)IOMalloc(count);if(!bytes)e=ENOBUFS;
  else {e=mbuf_copydata(data,0,count,bytes);if(!e)e=mapped(i9100_bt_worker_stream_write(c->worker,c,bytes,(unsigned)count));}
 }
 mbuf_freem(data);if(bytes){memset(bytes,0,count);IOFree(bytes,count);}callback_done(c);return e;
}
static errno_t set_option(kern_ctl_ref ref,uint32_t unit,void *token,int option,void *bytes,size_t length)
{
 auto *c=current(ref,token,unit,1);if(!c)return EPERM;
 int e=option==(int)0x802c7414U&&bytes&&length==44U?mapped(i9100_bt_worker_stream_attributes(c->worker,c,(uint8_t *)bytes,44,1)):ENOTSUP;
 callback_done(c);return e;
}
static errno_t get_option(kern_ctl_ref ref,uint32_t unit,void *token,int option,void *bytes,size_t *length)
{
 auto *c=current(ref,token,unit,1);if(!c)return EPERM;int e=0;
 if(option!=(int)0x402c7413U||!length)e=ENOTSUP;
 else if(!bytes){*length=44U;}
 else if(*length<44U){e=EMSGSIZE;} /* ctl_ctloutput panics if returned len exceeds input, even on error */
 else {e=mapped(i9100_bt_worker_stream_attributes(c->worker,c,(uint8_t *)bytes,44,0));if(!e)*length=44U;}
 callback_done(c);return e;
}
int i9100_bt_control_create(int enabled,i9100_bt_worker *worker,i9100_bt_control **owner)
{
 if(!owner||!worker||!ml_get_interrupts_enabled())return I9100_BTS_ARGUMENT;
 if(*owner)return I9100_BTS_BUSY;if(enabled!=1)return I9100_BTS_DISABLED;
 auto *c=new i9100_bt_control;if(!c)return I9100_BTS_IO;
 if(!c->initialize()){c->release();return I9100_BTS_IO;}c->lock=IOLockAlloc();c->worker=worker;
 if(!c->lock){c->release();return I9100_BTS_IO;}
 if(!swap(0,c)){c->release();return I9100_BTS_BUSY;}c->published=1;
 i9100_bt_stream_consumer consumer={c,consumer_retain,consumer_release,received};
 int e=i9100_bt_worker_stream_attach(worker,&consumer);if(!e){c->consumer_bound=1;
  struct kern_ctl_reg registration={};strlcpy(registration.ctl_name,"com.apple.uart.bluetooth",sizeof(registration.ctl_name));
  registration.ctl_flags=CTL_FLAG_REG_SOCK_STREAM;registration.ctl_sendsize=registration.ctl_recvsize=32768U;
  registration.ctl_connect=connected;registration.ctl_disconnect=disconnected;registration.ctl_send=sent;
  registration.ctl_setopt=set_option;registration.ctl_getopt=get_option;
  e=ctl_register(&registration,&c->reference);if(!e){IOLockLock(c->lock);c->state.registered=1;IOLockUnlock(c->lock);}
  else {int cleanup=i9100_bt_worker_stream_detach(worker);if(cleanup){IOLockLock(c->lock);c->state.error=e;c->state.cleanup_error=cleanup;c->state.closing=1;IOLockUnlock(c->lock);*owner=c;return e;}c->consumer_bound=0;}
 }
 if(e){IOLockLock(c->lock);c->state.closing=1;c->state.error=e;IOLockUnlock(c->lock);
  if(!swap(c,0)){*owner=c;return e;}c->published=0;
  if(__sync_val_compare_and_swap(&registry_readers,0U,0U)){*owner=c;return e;}
  c->release();return e;}*owner=c;return 0;
}
int i9100_bt_control_status(i9100_bt_control *c,struct i9100_bt_control_status *out)
{if(!c||!out||!ml_get_interrupts_enabled()||overlaps(c,sizeof(*c),out,sizeof(*out)))return I9100_BTS_ARGUMENT;
 IOLockLock(c->lock);*out=c->state;IOLockUnlock(c->lock);return 0;}
int i9100_bt_control_destroy(i9100_bt_control **owner)
{
 if(!owner||!*owner||!ml_get_interrupts_enabled())return I9100_BTS_ARGUMENT;auto *c=*owner;
 if(overlaps(c,sizeof(*c),owner,sizeof(*owner)))return I9100_BTS_ARGUMENT;
 IOLockLock(c->lock);if(c->state.client_active||c->connecting||c->borrowed){IOLockUnlock(c->lock);return I9100_BTS_BUSY;}
 c->state.closing=1;IOLockUnlock(c->lock);
 if(c->state.registered){int e=ctl_deregister(c->reference);IOLockLock(c->lock);if(e)c->state.cleanup_error=e;else c->state.registered=0;
  IOLockUnlock(c->lock);if(e)return e;}
 if(c->closing_token){int e=i9100_bt_worker_stream_close(c->worker,c);IOLockLock(c->lock);if(e)c->state.cleanup_error=e;else c->closing_token=0;
  IOLockUnlock(c->lock);if(e)return e;}
 if(c->published){if(!swap(c,0))return I9100_BTS_BUSY;c->published=0;}
 if(__sync_val_compare_and_swap(&registry_readers,0U,0U))return I9100_BTS_BUSY;
 if(c->consumer_bound){int e=i9100_bt_worker_stream_detach(c->worker);if(e){IOLockLock(c->lock);c->state.cleanup_error=e;IOLockUnlock(c->lock);return e;}c->consumer_bound=0;}
 c->release();*owner=0;return 0;
}
