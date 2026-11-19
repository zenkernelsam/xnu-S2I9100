/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_alias.h"
#include "IOS6I9100EMMCStorage.h"
#include <IOKit/storage/IOMedia.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <machine/machine_routines.h>
#include <string.h>
extern "C" int devfs_make_link(void *,char *,...);

/* Prepare in process context BEFORE BSD matching starts.
 * Lock order: BSD state -> alias gate -> storage -> native. No caller may
 * hold storage/native while entering this gate. Registry matching is always
 * notified after unlocking, since it may enter BSD or mount a volume.
 * Each slot owns BOTH the media and its exact provider even after detach. */
struct alias_slot {
 IOMedia *media;IOS6I9100EMMCStorage *provider;
 uint64_t ticket;bool linked,eligible,committed;
};
static IOLock *volatile alias_gate;
static alias_slot slots[2];
static uint64_t last_ticket;
static IOLock *gate()
{return __sync_val_compare_and_swap(&alias_gate,(IOLock *)0,(IOLock *)0);}
bool i9100_emmc_alias_prepare()
{
 if(ml_at_interrupt_context()||!ml_get_interrupts_enabled())return false;
 if(gate())return true;
 IOLock *fresh=IOLockAlloc();if(!fresh)return false;
 if(!__sync_bool_compare_and_swap(&alias_gate,(IOLock *)0,fresh))IOLockFree(fresh);
 return true; /* One gate intentionally lives for the kernel lifetime. */
}
static IOS6I9100EMMCStorage *copy_provider(IOMedia *m)
{
 IORegistryEntry *entry=m?m->copyParentEntry(gIOServicePlane):0;
 IOS6I9100EMMCStorage *p=OSDynamicCast(IOS6I9100EMMCStorage,entry);
 if(entry&&!p)entry->release();return p;
}
bool i9100_emmc_alias_required(IOMedia *m)
{IOS6I9100EMMCStorage *p=copy_provider(m);bool required=p!=0;if(p)p->release();return required;}
/* Gate held. All removable resource values still have a slot retain. */
static void withdraw_pair()
{
 IOService *resources=IOService::getResourceService();if(!resources)return;
 if(slots[0].media&&resources->getProperty("i9100-system-media")==slots[0].media)
  resources->removeProperty("i9100-system-media");
 if(slots[1].media&&resources->getProperty("i9100-data-media")==slots[1].media)
  resources->removeProperty("i9100-data-media");
}
int i9100_emmc_alias_publish(IOMedia *m,void *block,void *character,uint64_t *owned_ticket)
{
 if(!m||!owned_ticket)return -1;*owned_ticket=0;
 IOS6I9100EMMCStorage *provider=copy_provider(m);
 if(!provider)return 0; /* Other devices retain their original BSD path. */
 IOLock *lock=gate();if(!lock||!block||!character){provider->release();return -1;}
 IOLockLock(lock);
 unsigned role=provider->validateMedia(m,1U)?1U:provider->validateMedia(m,2U)?2U:0U;
 if(!role||slots[role-1U].media||last_ticket==~(uint64_t)0){IOLockUnlock(lock);provider->release();return -1;}
 alias_slot &slot=slots[role-1U];m->retain();slot.media=m;slot.provider=provider;
 slot.ticket=++last_ticket;slot.linked=false;slot.eligible=true;slot.committed=false;
 *owned_ticket=slot.ticket;
 bool ok=m->setProperty("I9100EMMCAliasOwned",role,32)&&m->setProperty("I9100EMMCAliasReady",false);
 char sys[]="i9100-system",data[]="i9100-data",rsys[]="ri9100-system",rdata[]="ri9100-data";
 int e=ok?devfs_make_link(block,role==1U?sys:data):-1;
 if(!e)e=devfs_make_link(character,role==1U?rsys:rdata);
 if(!e){slot.linked=true;ok=m->setProperty("I9100EMMCAliasLinked",true);}
 if(e||!ok)slot.eligible=false;
 IOLockUnlock(lock);
 /* Partial failure retains ownership until both original nodes are removed. */
 return e||!ok?-1:0;
}
uint64_t i9100_emmc_alias_ticket(IOMedia *m)
{
 IOLock *lock=gate();if(!m||!lock)return 0;IOLockLock(lock);uint64_t ticket=0;
 for(unsigned i=0;i<2;i++)if(slots[i].media==m)ticket=slots[i].ticket;
 IOLockUnlock(lock);return ticket;
}
static bool valid_pair()
{
 return slots[0].media&&slots[1].media&&slots[0].linked&&slots[1].linked&&
  slots[0].eligible&&slots[1].eligible&&slots[0].committed&&slots[1].committed&&
  slots[0].provider==slots[1].provider&&
  slots[0].provider->validateMedia(slots[0].media,1U)&&slots[1].provider->validateMedia(slots[1].media,2U);
}
void i9100_emmc_alias_commit(IOMedia *m,uint64_t ticket)
{
 IOLock *lock=gate();if(!m||!lock||!ticket)return;bool notify=false;IOLockLock(lock);
 for(unsigned i=0;i<2;i++){
  alias_slot &s=slots[i];
  if(s.media==m&&s.ticket==ticket&&s.eligible&&s.linked&&s.provider->validateMedia(m,i+1U)){
   if(m->setProperty("I9100EMMCAliasReady",true))s.committed=true;
  }
 }
 IOService *resources=IOService::getResourceService();
 if(valid_pair()&&resources){
  /* Same retained values and one gate serialize commit against invalidation.
   * Data prerequisite becomes visible first, system readiness last.
   * publishResource invokes matching under BSD/alias locks; use its actual
   * setProperty + registerService sequence, notifying only outside them. */
  if(resources->setProperty("i9100-data-media",(OSObject *)slots[1].media)&&
     resources->setProperty("i9100-system-media",(OSObject *)slots[0].media))notify=true;
  else withdraw_pair();
 }
 IOLockUnlock(lock);if(notify)resources->registerService();
}
IOMedia *i9100_emmc_alias_copy_root()
{
 IOLock *lock=gate();if(!lock)return 0;IOLockLock(lock);IOMedia *root=0;
 IOService *resources=IOService::getResourceService();
 if(valid_pair()&&resources&&resources->getProperty("i9100-system-media")==slots[0].media&&
    resources->getProperty("i9100-data-media")==slots[1].media){root=slots[0].media;root->retain();}
 IOLockUnlock(lock);return root;
}
void i9100_emmc_alias_invalidate(IOMedia *m)
{
 IOLock *lock=gate();if(!m||!lock)return;IOLockLock(lock);
 for(unsigned i=0;i<2;i++)if(slots[i].media==m){
  withdraw_pair();slots[i].eligible=false;slots[i].committed=false;
  m->setProperty("I9100EMMCAliasReady",false);
 }
 IOLockUnlock(lock);
}
void i9100_emmc_alias_unpublish(IOMedia *m,uint64_t ticket)
{
 IOLock *lock=gate();if(!m||!lock||!ticket)return;IOS6I9100EMMCStorage *provider=0;IOMedia *owned=0;
 IOLockLock(lock);
 for(unsigned i=0;i<2;i++)if(slots[i].media==m&&slots[i].ticket==ticket){
  withdraw_pair();owned=slots[i].media;provider=slots[i].provider;bzero(&slots[i],sizeof(slots[i]));
  m->setProperty("I9100EMMCAliasReady",false);m->setProperty("I9100EMMCAliasLinked",false);
  m->setProperty("I9100EMMCAliasOwned",(uint64_t)0,32);
 }
 IOLockUnlock(lock);
 /* Releases can destroy an object. Never run them while holding alias gate. */
 if(owned)owned->release();if(provider)provider->release();
}
