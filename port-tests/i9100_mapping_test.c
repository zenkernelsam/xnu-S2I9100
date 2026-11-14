#include <stdint.h>
#include <stddef.h>
typedef int SInt32;
typedef uint32_t ppnum_t,pt_entry_t,vm_map_offset_t,vm_offset_t,vm_size_t;typedef int boolean_t,kern_return_t,vm_prot_t,spl_t;
#define TRUE 1
#define FALSE 0
#define PAGE_SHIFT 12
#define PAGE_SIZE 4096U
#define PAGE_MASK 4095U
#define _1MB 0x100000U
#define L2_SIZE 1024U
#define L2_ADDR_MASK 0xfffff000U
#define L2_SMALL_PAGE 2U
#define L2_ACCESS_USER 0x30U
#define L2_ACCESS_APX 0x200U
#define L2_NX_BIT 1U
#define ARM_PAGE_MASK_VALUE 3U
#define ARM_PAGE_SECTION 2U
#define ARM_PAGE_PAGE_TABLE 1U
#define ARM_PTE_DESCRIPTOR_64K 1U
#define ARM_PTE_DESCRIPTOR_4K 2U
#define L1_SECT_ADDR_MASK 0xfff00000U
#define L1_PTE_ADDR(x) ((x)&0xfffffc00U)
#define tte_offset(x) (((x)>>20)*4U)
#define pte_offset(x) ((((x)>>12)&255U)*4U)
#define _COMM_PAGE_BASE_ADDRESS 0x40000000U
#define IOS7LAB_COMMPAGE_ALIAS 0xffff4000U
#define IOS7LAB_COMMPAGE_ALIAS6 0xffff1000U
#define PMAP_NULL ((pmap_t)0)
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_ALL 7
#define VM_MEM_MODIFIED 1
#define VM_MEM_REFERENCED 2
#define PMAP_OSPTE_TYPE_WIRED 1U
#define PMAP_OSPTE_TYPE_REFERENCED 2U
#define PMAP_OSPTE_TYPE_MODIFIED 4U
#define vm_page_fictitious_addr 0xffffffffU
#define LEO_BOOT_PAIRS 8192U
#define LEO_BOOT_RANGES 64U
struct leo_boot_pair{uint32_t va,pn;};struct leo_boot_range{uint32_t start,end,physical;};
static struct leo_boot_pair leo_boot_pairs[LEO_BOOT_PAIRS];static struct leo_boot_range leo_boot_ranges[LEO_BOOT_RANGES];
typedef struct q{struct q *next,*prev;}queue_head_t;
#define queue_next(p) ((p)->next)
#define queue_init(p) ((p)->next=(p)->prev=(p))
static void insque(queue_head_t *n,queue_head_t *p){n->next=p->next;n->prev=p;p->next->prev=n;p->next=n;}
static void remque(queue_head_t *p){p->next->prev=p->prev;p->prev->next=p->next;}
struct map{struct{int resident_count,wired_count;}pm_stats;int ledger[2];uint32_t pm_l1_size,pm_l1_virt;uint32_t ptes[16];};typedef struct map *pmap_t;
static struct map kernel,user,other;static pmap_t kernel_pmap=&kernel;
static int pmap_initialized=1,depth,pvdepth[4],hashdepth,fail_alloc,allocated,freed,syncs,execs,null_pte;
volatile unsigned i9100_pmap_empty_unwire[2];
static uint32_t npvhash=15,pmap_pv_hashlist_walks,pmap_pv_hashlist_cnts,pmap_pv_hashlist_max;
static uint32_t identityBaseVA=0x80000000,managedBaseVA=0xc0000000,sectionOffset,gPhysBase=0x42000000,gVirtBase=0x80000000;
static void semihost(uint32_t op,const void*p){register uint32_t r0 __asm__("r0")=op;register const void*r1 __asm__("r1")=p;__asm__ volatile("svc 0x123456":"+r"(r0):"r"(r1):"memory");}
static void __attribute__((noreturn)) finish(uint32_t c){uint32_t v[2]={0x20026,c};semihost(0x20,v);for(;;){}}
#define CHECK(x) do{if(!(x)){semihost(4,"FAIL line " STR(__LINE__) ": " #x "\n");finish(1);}}while(0)
#define STR_(x) #x
#define STR(x) STR_(x)
#define assert CHECK
#define panic(...) do{semihost(4,"unexpected panic: " __VA_ARGS__);finish(1);}while(0)
/* Only the selected negative control may turn a matching panic into success. */
static const char *expected_panic;
static void panic_failure(const char *fmt){
 const char *a=fmt,*b=expected_panic;
 if(b){while(*a&&*a==*b){a++;b++;}if(!*a&&!*b){semihost(4,"EXPECTED PANIC: ");semihost(4,fmt);semihost(4,"\n");finish(23);}}
 semihost(4,"unexpected panic: ");semihost(4,fmt);semihost(4,"\n");finish(1);
}
#undef panic
#define panic(fmt,...) panic_failure(fmt)
#define kprintf(...) ((void)0)
#define __improbable(x) (x)
#define CHK_NPVHASH() ((void)0)
#define ROOT_ABSENT 0
#define ROOT_PRESENT 1
#define PMAP_ACTION_ASSERT 0
#define PMAP_ACTION_IGNORE 1
#define PMAP_ACTION_RETRY 2
#define PMAP_ACTION_RETRY_RELOCK 3
typedef int pmap_pagetable_corruption_action_t;
static int pmap_classify_pagetable_corruption(void*a,uint32_t b,void*c,void*d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;return PMAP_ACTION_ASSERT;}
#define ppn_to_pai(x) (x)
#define PMAP_LOCK(m) do{(void)m;CHECK(!depth);depth++;}while(0)
#define PMAP_UNLOCK(m) do{(void)m;CHECK(depth==1);depth--;}while(0)
#define LOCK_PVH(p) do{CHECK((p)>=0x42000&&(p)<0x42004);CHECK(!pvdepth[(p)-0x42000]);pvdepth[(p)-0x42000]++;}while(0)
#define UNLOCK_PVH(p) do{CHECK(pvdepth[(p)-0x42000]==1);pvdepth[(p)-0x42000]--;}while(0)
#define LOCK_PV_HASH(b) do{(void)b;CHECK(!hashdepth);hashdepth++;}while(0)
#define UNLOCK_PV_HASH(b) do{(void)b;CHECK(hashdepth==1);hashdepth--;}while(0)
#define OSAddAtomic(n,p) (*(p)+=(n))
#define phys_to_virt(p) (p)
static struct{int phys_mem,wired_mem;}task_ledgers={0,1};
static void pmap_ledger_credit(pmap_t m,int w,int n){CHECK(n==4096);m->ledger[w]++;}
static void pmap_ledger_debit(pmap_t m,int w,int n){CHECK(n==4096);m->ledger[w]--;}
static int pmap_valid_page(uint32_t p){return p>=0x42000&&p<0x42004;}
static uint32_t managed_ptes[16];
static uint32_t pmap_pte(pmap_t m,uint32_t v){if(null_pte)return 0;if(m==kernel_pmap&&v>=0xc0000000U)return (uint32_t)&managed_ptes[(v>>12)&15U];return (uint32_t)&m->ptes[(v>>12)&15U];}
static void pmap_expand(pmap_t m,uint32_t v){(void)m;(void)v;CHECK(0);}
static uint32_t pmap_vm_prot_to_page_flags(pmap_t m,int prot,int wired,int f){(void)m;(void)wired;(void)f;return (prot&2)?0x10:0x210;}
static uint32_t pmap_get_cache_attributes(uint32_t p){return pmap_valid_page(p)?0x4c:0;}
static int i9100_cache_policy(uint32_t p,int f){(void)f;return pmap_valid_page(p>>12);}
static void i9100_sync_executable(uint32_t p){CHECK(pmap_valid_page(p));execs++;}
static void pte_sync(const void*p){(void)p;syncs++;}
static void pmap_flush_tlbs(pmap_t m,uint32_t a,uint32_t b){(void)m;CHECK(b>=a);}
static void *pv_hashed_list_zone;
static void *allocate_node(void);static void free_nodes(void*,void*,int);static void *zalloc(void*z){(void)z;CHECK(!depth&&!hashdepth);for(int n=0;n<4;n++)CHECK(!pvdepth[n]);return allocate_node();}
#define PV_HASHED_ALLOC(p) do{if(fail_alloc){fail_alloc=0;p=0;}else p=allocate_node();}while(0)
#define PV_HASHED_KERN_ALLOC(p) ((p)=allocate_node())
#define PV_HASHED_FREE_LIST(a,b,c) free_nodes(a,b,c)
static void phys_attribute_clear(uint32_t,int);
#define pai_to_pvh(p) (&roots[(p)-0x42000])
#define pvhash(b) (&hashes[b])
/* Structures before the extracted function block need storage declared here. */
#include "mapping-structs.inc"
typedef queue_head_t *queue_entry_t;
static struct pv_rooted_entry roots[4];static pv_hashed_entry_t hashes[16];
static struct pv_hashed_entry pool[128];
static void *allocate_node(void){CHECK(allocated<128);return &pool[allocated++];}
static void free_nodes(void*a,void*b,int n){(void)a;(void)b;freed+=n;}
static void phys_attribute_clear(uint32_t p,int f){roots[p-0x42000].flags&=~f;}
static void phys_attribute_set(uint32_t p,int f){roots[p-0x42000].flags|=f;}
static int phys_attribute_test(uint32_t p,int f){return roots[p-0x42000].flags&f;}
#include "mapping-under-test.inc"
static void enter(pmap_t m,uint32_t va,uint32_t pn,int w){CHECK(pmap_enter_options(m,va,pn,7,0,0,w,0)==0);CHECK(!depth&&!hashdepth);for(int n=0;n<4;n++)CHECK(!pvdepth[n]);}
static void remove_one(pmap_t m,uint32_t va){PMAP_LOCK(m);pt_entry_t *p=(pt_entry_t*)pmap_pte(m,va);pmap_remove_range(m,va,p,p+1,0);PMAP_UNLOCK(m);}
static void balances(pmap_t m,int resident,int wired){CHECK(m->pm_stats.resident_count==resident&&m->ledger[0]==resident);CHECK(m->pm_stats.wired_count==wired&&m->ledger[1]==wired);}
static uint32_t l1a[4096] __attribute__((aligned(16384))),l1b[4096] __attribute__((aligned(16384)));
static uint32_t l2a[256] __attribute__((aligned(1024))),l2b[256] __attribute__((aligned(1024)));
void run(void){
#ifdef UNWIRE_REJECT_CASE
 // Missing wire target, user-map hole, absent table, malformed nonzero PTE,
 // valid mapping without PV, preinit hole and broken ledger stay fatal.
 expected_panic="pmap_change_wiring valid small PTE missing map=%p va=0x%08x wired=%u pte=%p value=0x%08x caller=%p";
#if UNWIRE_REJECT_CASE == 1
 pmap_change_wiring(&kernel,0xe7545000,TRUE);
#elif UNWIRE_REJECT_CASE == 2
 pmap_change_wiring(&user,0x5000,FALSE);
#elif UNWIRE_REJECT_CASE == 3
 null_pte=1;pmap_change_wiring(&kernel,0xe7545000,FALSE);
#elif UNWIRE_REJECT_CASE == 4
 managed_ptes[5]=1;pmap_change_wiring(&kernel,0xe7545000,FALSE);
#elif UNWIRE_REJECT_CASE == 5
 expected_panic="pmap_change_wiring tracked PV missing";
 managed_ptes[5]=0x4200004e;pmap_change_wiring(&kernel,0xe7545000,FALSE);
#elif UNWIRE_REJECT_CASE == 6
 pmap_initialized=0;pmap_change_wiring(&kernel,0xe7545000,FALSE);
#else
 enter(&kernel,0xe7545000,0x42000,1);kernel.pm_stats.wired_count=0;
 expected_panic="pmap mapping wired_count";pmap_change_wiring(&kernel,0xe7545000,FALSE);
#endif
 CHECK(0);
#endif
 // First/repeated wire and two aliases: removing root promotes the wired alias.
 enter(&user,0x1000,0x42000,0);balances(&user,1,0);
 pmap_change_wiring(&user,0x1000,1);pmap_change_wiring(&user,0x1000,1);balances(&user,1,1);
 enter(&user,0x2000,0x42000,1);balances(&user,2,2);
 pmap_change_wiring(&user,0x1000,0);pmap_change_wiring(&user,0x1000,0);balances(&user,2,1);CHECK(roots[0].flags&1);
 remove_one(&user,0x1000);balances(&user,1,1);CHECK(roots[0].mapping_wired&&roots[0].va==0x2000);
 // Same-page enter, then RAM->MMIO replacement, then MMIO unwire/repeat/removal.
 enter(&user,0x2000,0x42000,1);balances(&user,1,1);
 enter(&user,0x2000,0x42000,0);balances(&user,1,0);CHECK(!(roots[0].flags&1));
 enter(&user,0x2000,0x42000,0);balances(&user,1,0);
 enter(&user,0x2000,0x42000,1);balances(&user,1,1);
 enter(&user,0x2000,0x11c00,1);balances(&user,1,1);CHECK(!(roots[0].flags&1));
 pmap_change_wiring(&user,0x2000,0);pmap_change_wiring(&user,0x2000,0);balances(&user,1,0);
 enter(&user,0x2000,0x11c00,1);balances(&user,1,1);remove_one(&user,0x2000);balances(&user,0,0);
 // Distinct pmaps wired to the same RAM page maintain separate ledgers.
 enter(&user,0x3000,0x42001,1);enter(&other,0x3000,0x42001,1);
 remove_one(&user,0x3000);balances(&user,0,0);balances(&other,1,1);CHECK(roots[1].mapping_wired);
 pmap_page_protect(0x42001,0);balances(&other,0,0);CHECK(!roots[1].pmap&&!(roots[1].flags&1));
 // Root and hash aliases removed together by page_protect, including kernel alias.
 enter(&kernel,0x4000,0x42002,1);enter(&user,0x4000,0x42002,1);enter(&other,0x5000,0x42002,0);
 pmap_page_protect(0x42002,0);balances(&kernel,0,0);balances(&user,0,0);balances(&other,0,0);
 // Read protection of user mapping retains wire state, kernel removal debits it.
 enter(&kernel,0x6000,0x42003,1);enter(&user,0x6000,0x42003,1);
 pmap_page_protect(0x42003,VM_PROT_READ);balances(&kernel,0,0);balances(&user,1,1);CHECK(roots[3].mapping_wired);
#ifdef UNWIRE_OLD_CONTROL
 expected_panic="pmap_change_wiring valid small PTE missing map=%p va=0x%08x wired=%u pte=%p value=0x%08x caller=%p";
#endif
 // The kernel mapping has already gone, but VM still unwires its object.
 // The surviving user's rooted PV and wired ledger must remain untouched.
 int old_allocated=allocated,old_freed=freed,old_syncs=syncs;
 pmap_change_wiring(&kernel,0x6000,FALSE);pmap_change_wiring(&kernel,0x6000,FALSE);
#ifdef UNWIRE_OLD_CONTROL
 CHECK(0); // A control using already-fixed source must not pass.
#endif
 CHECK(allocated==old_allocated&&freed==old_freed&&syncs==old_syncs);
 balances(&kernel,0,0);balances(&user,1,1);CHECK(roots[3].mapping_wired&&roots[3].pmap==&user);
 CHECK(user.ptes[6]&L2_ACCESS_APX);remove_one(&user,0x6000);balances(&user,0,0);
 // Unlocked allocation retry for a user alias and hash-only unmanaged mapping.
 enter(&kernel,0x7000,0x42000,0);fail_alloc=1;enter(&user,0x7000,0x42000,1);remove_one(&user,0x7000);remove_one(&kernel,0x7000);
 fail_alloc=1;enter(&user,0x8000,0x11c00,1);enter(&user,0x8000,0x42000,0);balances(&user,1,0);remove_one(&user,0x8000);
 // A replacement retries allocation after removing old PV, without double debit.
 enter(&user,0xa000,0x42000,1);enter(&kernel,0xb000,0x42001,0);
 fail_alloc=1;enter(&user,0xa000,0x42001,1);balances(&user,1,1);CHECK(!roots[0].pmap);
 remove_one(&user,0xa000);remove_one(&kernel,0xb000);balances(&user,0,0);balances(&kernel,0,0);
 // Two MMIO aliases use independent hash-only wire records.
 enter(&user,0xc000,0x11c00,1);enter(&user,0xd000,0x11c00,1);balances(&user,2,2);
 remove_one(&user,0xc000);balances(&user,1,1);pmap_change_wiring(&user,0xd000,0);
 balances(&user,1,0);remove_one(&user,0xd000);balances(&user,0,0);
 // Exact panic VA: full kernel removal then late/repeated unwire, including
 // a promoted surviving alias, MMIO and reuse of the same virtual address.
 for(unsigned n=0;n<20;n++){
  enter(&kernel,0xe7545000,0x42002,1);enter(&user,0x5000,0x42002,1);
  remove_one(&kernel,0xe7545000);CHECK(managed_ptes[5]==0);
  pmap_change_wiring(&kernel,0xe7545000,FALSE);pmap_change_wiring(&kernel,0xe7545000,FALSE);
  balances(&kernel,0,0);balances(&user,1,1);CHECK(roots[2].pmap==&user&&roots[2].mapping_wired);
  remove_one(&user,0x5000);CHECK(!roots[2].pmap&&!(roots[2].flags&1));
 }
 enter(&kernel,0xe7545000,0x11c00,1);remove_one(&kernel,0xe7545000);
 pmap_change_wiring(&kernel,0xe7545000,FALSE);balances(&kernel,0,0);
 CHECK(i9100_pmap_empty_unwire[0]==43&&i9100_pmap_empty_unwire[1]==0xe7545000);
 CHECK(!depth&&!hashdepth&&allocated==freed);for(unsigned n=0;n<4;n++)CHECK(!pvdepth[n]);
 semihost(4,"ARM late kernel unwire: removed/protected mapping, surviving PV alias, MMIO and20 VA reuse cycles PASS\n");
 // Bootstrap mappings have explicit provenance, no invented wire status/ledger.
 pmap_initialized=0;enter(&kernel,0x9000,0x11c00,1);balances(&kernel,0,0);
 pmap_initialized=1;pmap_change_wiring(&kernel,0x9000,1);balances(&kernel,1,1);remove_one(&kernel,0x9000);balances(&kernel,0,0);
 // Validate initial direct mapping provenance seeding against the actual loop.
 gPhysBase=0x42001000;gVirtBase=identityBaseVA=0x80001000;sectionOffset=4096;
 for(int n=0;n<16;n++)kernel.ptes[n]=((gPhysBase-sectionOffset)+n*4096)|0x4e;
 managed_ptes[0]=0x4200a04e;
 leo_boot_seed_linear(16*4096);CHECK(leo_boot_match(0x80007000,0x42007));CHECK(!leo_boot_match(0x80007000,0x42008));CHECK(!leo_boot_forget(0x80007000,0x42008));
 CHECK(leo_boot_forget(0x80007000,0x42007));CHECK(!leo_boot_match(0x80007000,0x42007));CHECK(leo_boot_match(0x80008000,0x42008));CHECK(leo_boot_match(0xc0000000,0x4200a));
 pmap_initialized=0;leo_boot_bd_replace(0x8000a000,0x50000000,4096);
 kernel.ptes[10]=0x5000004e;CHECK(leo_boot_match(0x8000a000,0x50000));CHECK(!leo_boot_match(0x8000a000,0x4200a));pmap_initialized=1;
 // Software extraction selects the requested L1 and supports all short formats.
 user.pm_l1_size=other.pm_l1_size=kernel.pm_l1_size=16384;
 user.pm_l1_virt=(uint32_t)l1a;other.pm_l1_virt=(uint32_t)l1b;
 l1a[0]=(uint32_t)l2a|1;l1b[0]=(uint32_t)l2b|1;
 l2a[1]=0x4200104e;l2b[1]=0x4200204f;
 CHECK(pmap_extract(&user,0x1234)==0x42001234);CHECK(pmap_extract(&other,0x1234)==0x42002234);
 l2a[1]=0x42001000;CHECK(pmap_extract(&user,0x1234)==0);
 l2a[1]=0x42010001;CHECK(pmap_extract(&user,0x1234)==0x42011234);
 l1a[0]=0x42000002;CHECK(pmap_extract(&user,0x1234)==0x42001234);
 user.pm_l1_size=4;CHECK(pmap_extract(&user,0x100000)==0);CHECK(pmap_extract(&user,0x40000000)==0);
 roots[0].flags=7;pmap_clear_refmod(0x42000,VM_MEM_MODIFIED);CHECK(roots[0].flags==3);
 pmap_clear_refmod(0x42000,VM_MEM_REFERENCED);CHECK(roots[0].flags==1);
 CHECK(!depth&&!hashdepth&&syncs>0&&execs>0);CHECK(allocated==freed);
 CHECK(pmap_enter_options(&user,0x40000000,0x42000,7,0,0,0,0)==KERN_INVALID_ARGUMENT);
#ifdef RM_TEST
 // Actual disconnect must return already recorded dirty state to the pager.
 roots[0].flags=0;enter(&user,0x1000,0x42000,0);
 phys_attribute_set(0x42000,PMAP_OSPTE_TYPE_MODIFIED);
 unsigned rm=pmap_disconnect(0x42000);
#ifdef RM_OLD_CONTROL
 CHECK(!(rm&VM_MEM_MODIFIED));
 semihost(4,"OLD CONTROL: actual pmap_disconnect erased recorded modify state; VM would see the dirty slab page as clean\n");finish(23);
#endif
 CHECK(rm&VM_MEM_MODIFIED);CHECK(rm&VM_MEM_REFERENCED);balances(&user,0,0);
 // Clearing software state cannot hide a subsequent write through live RW PTE.
 roots[0].flags=0;enter(&user,0x1000,0x42000,1);
 pmap_clear_modify(0x42000);pmap_clear_reference(0x42000);
 CHECK(roots[0].flags==1);CHECK(pmap_is_modified(0x42000));CHECK(pmap_is_referenced(0x42000));
 CHECK((pmap_get_refmod(0x42000)&3)==3);remove_one(&user,0x1000);
 CHECK((pmap_get_refmod(0x42000)&3)==3);
 // Genuine RO page stays clean; retained unrelated physical attributes survive.
 pmap_clear_refmod(0x42000,3);roots[0].flags|=0x18;
 CHECK(!pmap_is_modified(0x42000)&&!pmap_is_referenced(0x42000));
 CHECK(pmap_enter_options(&user,0x1000,0x42000,VM_PROT_READ,0,0,0,0)==0);
 CHECK(!pmap_is_modified(0x42000)&&pmap_is_referenced(0x42000));
 CHECK(!(pmap_disconnect(0x42000)&VM_MEM_MODIFIED));CHECK((roots[0].flags&0x18)==0x18);
 // Query must find writable hashed alias when rooted alias is read-only.
 pmap_clear_refmod(0x42000,3);
 CHECK(pmap_enter_options(&user,0x1000,0x42000,VM_PROT_READ,0,0,0,0)==0);
 enter(&other,0x2000,0x42000,0);pmap_clear_refmod(0x42000,3);
 CHECK(pmap_get_refmod(0x42000)&VM_MEM_MODIFIED);
 pmap_clear_refmod(0x42000,3);remove_one(&other,0x2000);
 CHECK(pmap_get_refmod(0x42000)&VM_MEM_MODIFIED);
 pmap_clear_modify(0x42000);CHECK(!pmap_is_modified(0x42000));remove_one(&user,0x1000);
 // RW->RO must harvest permissions before losing the write evidence.
 enter(&user,0x1000,0x42000,0);pmap_clear_refmod(0x42000,3);
 pmap_page_protect(0x42000,VM_PROT_READ);CHECK(pmap_is_modified(0x42000));
 pmap_clear_modify(0x42000);CHECK(!pmap_is_modified(0x42000));remove_one(&user,0x1000);
 // Replacement preserves the retiring PA state and independently marks new PA.
 enter(&user,0x1000,0x42000,0);pmap_clear_refmod(0x42000,3);
 enter(&user,0x1000,0x42001,0);CHECK(pmap_is_modified(0x42000));CHECK(pmap_is_modified(0x42001));
 remove_one(&user,0x1000);pmap_clear_refmod(0x42000,3);pmap_clear_refmod(0x42001,3);
 CHECK(!pmap_get_refmod(0x42000)&&!pmap_get_refmod(0x42001));
 // No physical-root lookup for MMIO, preinit or untracked bootstrap linear RW.
 CHECK(!pmap_get_refmod(0x11c00)&&!pmap_is_modified(0x11c00));
 pmap_clear_refmod(0x11c00,3);pmap_initialized=0;CHECK(!pmap_get_refmod(0x42000));pmap_initialized=1;
 CHECK(!pmap_get_refmod(0x42000));
 for(unsigned n=0;n<20;n++) {
  enter(&user,0x1000,0x42000,0);pmap_clear_refmod(0x42000,3);
  CHECK(pmap_disconnect(0x42000)&VM_MEM_MODIFIED);pmap_clear_refmod(0x42000,3);
  CHECK(!pmap_is_modified(0x42000)&&!pmap_is_referenced(0x42000));
 }
 CHECK(!depth&&!hashdepth&&allocated==freed);for(unsigned n=0;n<4;n++)CHECK(!pvdepth[n]);
 semihost(4,"ARM ref/mod: actual disconnect returns dirty state; live RW after clear, readonly cleanliness, hashed alias scan, downgrade/removal/replacement retention, MMIO/bootstrap exclusion and20 reuse cycles PASS\n");
#endif
 semihost(4,"ARM mapping lifecycle: wire/unwire idempotency, aliases, PV promotion, RAM/MMIO replacement, page protection, ledgers, allocation retry, bootstrap adoption/seeding, cache publication, target-map translation and ref/mod bits passed\n");finish(0);
}
