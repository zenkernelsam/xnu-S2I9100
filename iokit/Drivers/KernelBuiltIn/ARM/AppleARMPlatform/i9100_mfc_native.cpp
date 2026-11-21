/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_native.h"
#include "i9100_mfc_board_native.h"
#include "i9100_mfc_power.h"
#include "i9100_dma_native.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <mach/vm_param.h>
#include <string.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <vm/pmap.h>
vm_offset_t pmap_pte(pmap_t,vm_offset_t);
extern unsigned long gPhysBase,gVirtBase;
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
uint32_t i9100_camera_mmio_read(volatile uint8_t *,unsigned);
void i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
int i9100_bcm_fw_kernel_sha256(void *,const void *,uint32_t,uint8_t[32]);
}
enum {MN_MFC,MN_MMU_L,MN_MMU_R,MN_CMU,MN_PMU,MN_GIC,MN_MAPS};
static volatile uint32_t mfc_reserved;
struct i9100_mfc_native {
 i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;
 i9100_mfc_supply supply;i9100_mfc_supply_lease supply_lease;
 i9100_mfc_host_lease host;
 IOLock *lifecycle,*engine;
 IOMemoryDescriptor *memory[MN_MAPS];IOMemoryMap *mapping[MN_MAPS];volatile uint8_t *regs[MN_MAPS];
 i9100_dma_native *allocation[2];i9100_mfc core; i9100_mfc_decoder decoder;
 uint32_t phase,reserved,source,divider,stream_supplied,decode_enabled,codec_frozen;int error,cleanup_error;
 uint32_t holds;uintptr_t hold_tokens[4];
 struct {uintptr_t token;struct i9100_mfc_board *board;uint32_t generation,irq_generation,bound;} ctl;
};
static int take(IOLock *l){return l&&ml_get_interrupts_enabled()&&IOLockTryLock(l)?0:-1;}
static uint32_t rd(i9100_mfc_native *n,unsigned map,unsigned off)
{return !n->regs[map]?0xffffffffU:i9100_camera_mmio_read(n->regs[map],off);}
static uint32_t engine_read(void *p,unsigned off){return rd((i9100_mfc_native *)p,MN_MFC,off);}
static void engine_write(void *p,unsigned off,uint32_t value)
{i9100_mfc_native *n=(i9100_mfc_native *)p;if(n->regs[MN_MFC])i9100_camera_mmio_write(n->regs[MN_MFC],off,value);}
static int engine_take(void *p){return take(((i9100_mfc_native *)p)->engine);}
static void engine_drop(void *p){IOLockUnlock(((i9100_mfc_native *)p)->engine);}
static uint64_t now(void *){uint64_t n=0;absolutetime_to_nanoseconds(mach_absolute_time(),&n);return n;}
static int delay(void *,uint32_t us){if(!ml_get_interrupts_enabled()||us>10000U)return -1;IODelay(us);return 0;}
static void barrier(void *){OSSynchronizeIO();}
static int hash(void *,const void *bytes,uint32_t size,uint8_t digest[32])
{return i9100_bcm_fw_kernel_sha256(0,bytes,size,digest);}
static bool supply_valid(i9100_mfc_native *n)
{return n->supply_lease.self==(uintptr_t)&n->supply_lease&&n->supply_lease.owned==1U&&n->supply_lease.provider&&
 n->supply_lease.generation&&!n->supply.validate(n->supply.cookie,&n->supply_lease);}
static int resources(i9100_mfc_native *n)
{
 int e=i9100_mfc_host_check(&n->host);
 if(e==I9100_MP_BUSY)return I9100_MFC_BUSY; /* gate held elsewhere: defer, not ownership loss */
 if(e)return I9100_MFC_OWNER;
 {
  struct i9100_mfc_supply_lease *l=&n->supply_lease;
  e=(l->self!=(uintptr_t)l||l->owned!=1U||!l->provider||!l->generation)?I9100_MFC_OWNER:
   n->supply.validate(n->supply.cookie,l);
  if(e==I9100_MP_BUSY)return I9100_MFC_BUSY;
  if(e)return I9100_MFC_OWNER;
 }
 uint32_t gate=rd(n,MN_CMU,0xc928),power=rd(n,MN_PMU,0x3c40),status=rd(n,MN_PMU,0x3c44);
 uint32_t source=rd(n,MN_CMU,0xc228),divider=rd(n,MN_CMU,0xc528),irq=rd(n,MN_GIC,0x10c);
 if(gate==0xffffffffU||power==0xffffffffU||status==0xffffffffU||source==0xffffffffU||divider==0xffffffffU||irq==0xffffffffU||
    (gate&7U)!=7U||(power&7U)!=7U||(status&7U)!=7U||((irq&(1U<<30))&&!n->ctl.bound))return I9100_MFC_OWNER;
 if(n->phase!=I9100_MFC_N_BUILDING&&((source&0x111U)!=(n->source&0x111U)||(divider&15U)!=(n->divider&15U)))return I9100_MFC_OWNER;
 for(unsigned unit=MN_MMU_L;unit<=MN_MMU_R;unit++)if(rd(n,unit,0)||rd(n,unit,0x18))return I9100_MFC_OWNER;
 return 0;
}
static int idle(void *p)
{i9100_mfc_native *n=(i9100_mfc_native *)p;return resources(n)||!n->core.initialized?-1:i9100_mfc_idle(&n->core);}
static int normal_nc(void *p,const i9100_dma_buffer *a,const i9100_dma_buffer *b)
{
 i9100_mfc_native *n=(i9100_mfc_native *)p;
 int e=resources(n);
 if(e==I9100_MFC_BUSY)return I9100_MFC_BUSY; /* transient gate contention: caller retries the reply */
 if(e||a!=i9100_dma_native_buffer(n->allocation[0])||b!=i9100_dma_native_buffer(n->allocation[1]))return -1;
 const i9100_dma_buffer *banks[2]={a,b};
 for(unsigned k=0;k<2;k++){
  const i9100_dma_buffer *v=banks[k];
  if(!v||v->self!=(uintptr_t)v||!v->holds||!v->cpu||!v->bytes||(v->bytes&4095U)||
     v->bytes>8U*1024U*1024U||v->physical<gPhysBase||(uint64_t)v->physical+v->bytes>0x80000000ULL||
     v->cpu>~(uintptr_t)0-v->bytes)return -1;
  for(unsigned off=0;off<v->bytes;off+=4096U){
   vm_offset_t cpu=pmap_pte(kernel_pmap,(vm_offset_t)v->cpu+off);
   vm_offset_t linear=pmap_pte(kernel_pmap,((vm_offset_t)v->physical+off-gPhysBase)+gVirtBase);
   uint32_t pa=v->physical+off,cv=cpu?*(volatile uint32_t *)cpu:0,lv=linear?*(volatile uint32_t *)linear:0;
   if(!(cv&2U)||!(lv&2U)||(cv&0xfffff000U)!=pa||(lv&0xfffff000U)!=pa||
      (cv&0x1ccU)!=0x40U||(lv&0x1ccU)!=0x40U)return -1;
  }
 }OSSynchronizeIO();return 0;
}
static int cleanup(i9100_mfc_native *n)
{
 if(n->core.enabled||n->core.staged||n->allocation[0]||n->allocation[1]){
  int e=resources(n);if(e)return e;
  if(n->core.initialized){e=i9100_mfc_stop(&n->core);if(e)return e;
   if(n->core.enabled){e=i9100_mfc_enable(&n->core,0,0);if(e)return e;}}
  for(unsigned i=0;i<2;i++)if(n->allocation[i]){
   i9100_dma_buffer *b=i9100_dma_native_buffer(n->allocation[i]);
   if(b&&b->state==I9100_DMA_QUARANTINED){e=i9100_dma_abort(b);if(e)return e;}
   e=i9100_dma_native_destroy(&n->allocation[i]);if(e)return e;
  }
 }
 if(n->supply_lease.owned){int e=n->supply.release(n->supply.cookie,&n->supply_lease);if(e||n->supply_lease.owned)return e?e:I9100_MFC_OWNER;}
 for(unsigned i=0;i<MN_MAPS;i++){
  if(n->mapping[i]){n->mapping[i]->release();n->mapping[i]=0;n->regs[i]=0;}
  if(n->memory[i]){n->memory[i]->release();n->memory[i]=0;}
 }
 if(n->parent_hold.owned){int e=i9100_peripheral_native_hold_put(&n->parent_hold);if(e)return e;}
 if(n->host.owned){int e=i9100_mfc_host_release(&n->host,1);if(e)return e;}
 if(n->reserved){__sync_lock_release(&mfc_reserved);n->reserved=0;}return 0;
}
static void dispose(i9100_mfc_native *n)
{if(n->lifecycle)IOLockFree(n->lifecycle);if(n->engine)IOLockFree(n->engine);IOFree(n,sizeof(*n));}
static int fault(i9100_mfc_native *n,int e)
{
 if(!n->error)n->error=e;n->phase=I9100_MFC_N_FAILED;
 if(n->core.fault)n->cleanup_error=n->core.cleanup_error;
 else if(!resources(n))n->cleanup_error=i9100_mfc_stop(&n->core);return e;
}
static int open_native(int enabled,i9100_peripheral_native *parent,const i9100_mfc_supply *supply,
 const i9100_mfc_decode_config *config,i9100_mfc_native **out)
{
 if(!out||*out||(enabled!=0&&enabled!=1))return I9100_MFC_ARGUMENT;if(!enabled)return I9100_MFC_DISABLED;
 if(config&&(config->board_data_reviewed!=1U||config->bank_a_bytes<1024U*1024U||config->bank_a_bytes>8U*1024U*1024U||
    config->bank_b_bytes<1024U*1024U||config->bank_b_bytes>8U*1024U*1024U||
    ((config->bank_a_bytes|config->bank_b_bytes)&4095U)))return I9100_MFC_ARGUMENT;
 if(!parent||!supply||!supply->acquire||!supply->validate||!supply->release||!ml_get_interrupts_enabled())return I9100_MFC_ARGUMENT;
 if(!__sync_bool_compare_and_swap(&mfc_reserved,0U,1U))return I9100_MFC_BUSY;
 i9100_mfc_native *n=(i9100_mfc_native *)IOMalloc(sizeof(*n));if(!n){__sync_lock_release(&mfc_reserved);return I9100_MFC_IO;}
 memset(n,0,sizeof(*n));n->reserved=1;n->parent=parent;n->supply=*supply;n->phase=I9100_MFC_N_BUILDING;n->decode_enabled=config!=0;
 int e=i9100_peripheral_native_hold_get(parent,&n->parent_hold);if(e){__sync_lock_release(&mfc_reserved);dispose(n);return e;}*out=n;
 n->lifecycle=IOLockAlloc();n->engine=IOLockAlloc();if(!n->lifecycle||!n->engine){e=I9100_MFC_IO;goto fail;}
 e=i9100_mfc_host_acquire(supply->host,&n->host);if(e)goto fail;
 e=n->supply.acquire(n->supply.cookie,&n->supply_lease);if(e)goto fail;if(!supply_valid(n)){e=I9100_MFC_OWNER;goto fail;}
 {
  const uint32_t phys[MN_MAPS]={0x13400000U,0x13620000U,0x13630000U,0x10030000U,0x10020000U,0x10490000U};
  const uint32_t lengths[MN_MAPS]={65536,4096,4096,65536,16384,4096};
  for(unsigned k=0;k<MN_MAPS;k++){
   n->memory[k]=IOMemoryDescriptor::withPhysicalAddress(phys[k],lengths[k],kIODirectionInOut);
   n->mapping[k]=n->memory[k]?n->memory[k]->map(kIOMapInhibitCache):0;
   n->regs[k]=n->mapping[k]?(volatile uint8_t *)n->mapping[k]->getAddress():0;if(!n->regs[k]){e=I9100_MFC_IO;goto fail;}
  }
 }
 e=resources(n);if(e)goto fail;n->source=rd(n,MN_CMU,0xc228);n->divider=rd(n,MN_CMU,0xc528);
 {
  i9100_mfc_io io={n,engine_read,engine_write,engine_take,engine_drop,now,delay,barrier,hash};
  e=i9100_mfc_init(&n->core,&io);if(!e)e=i9100_mfc_enable(&n->core,1,1);if(e)goto fail;
  const uint32_t sizes[2]={config?config->bank_a_bytes:8U*1024U*1024U,config?config->bank_b_bytes:1024U*1024U};
  for(unsigned k=0;k<2;k++){
   e=i9100_dma_native_allocate_aligned(1,sizes[k],I9100_MFC_BASE_ALIGNMENT,n,idle,&n->allocation[k]);if(e)goto fail;
   e=i9100_dma_enable(i9100_dma_native_buffer(n->allocation[k]),1);if(e)goto fail;
  }
 }
 n->phase=I9100_MFC_N_PREPARED;return 0;
fail:n->error=e;n->phase=I9100_MFC_N_CLOSING;
 {int off=cleanup(n);n->cleanup_error=off;if(!off){dispose(n);*out=0;}}return e;
}
int i9100_mfc_native_open(int enabled,i9100_peripheral_native *parent,const i9100_mfc_supply *supply,i9100_mfc_native **out)
{return open_native(enabled,parent,supply,0,out);}
int i9100_mfc_native_open_decode(int enabled,i9100_peripheral_native *parent,const i9100_mfc_supply *supply,
 const i9100_mfc_decode_config *config,i9100_mfc_native **out)
{if(!config&&enabled)return I9100_MFC_ARGUMENT;return open_native(enabled,parent,supply,config,out);}
/* The exclusive controller lease: the board that issued our supply lease
 * must actually verify the handed IRQ-owner token (identity, ownership and
 * issuing generation) at bind time and at every command entry. An unissued,
 * copied or stale integer is refused before any hardware/fault path; a
 * legitimate bound owner's real resource damage still latches. While bound,
 * raw transitions and masked polling are refused; a lifetime hold never
 * grants command or IRQ-armed exemption. */
static int control(i9100_mfc_native *n,uintptr_t ctl,int raw)
{
 if(!n->ctl.bound)return ctl?I9100_MFC_OWNER:0;
 if(!ctl)return raw;
 if(ctl!=n->ctl.token||n->ctl.generation!=n->supply_lease.generation)return I9100_MFC_OWNER;
 unsigned generation=0;
 int e=i9100_mfc_board_irq_verify(n->ctl.board,ctl,&generation);
 if(e==I9100_MP_BUSY)return I9100_MFC_BUSY;
 if(e||generation!=n->ctl.irq_generation)return I9100_MFC_OWNER;
 return 0;
}
int i9100_mfc_native_control_bind(i9100_mfc_native *n,uintptr_t ctl)
{
 if(!n||!ctl)return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;
 int e=n->phase==I9100_MFC_N_CLOSING||n->phase==I9100_MFC_N_FAILED||n->error?I9100_MFC_OWNER:
  n->ctl.bound?I9100_MFC_BUSY:0;
 i9100_mfc_board *b=(i9100_mfc_board *)n->supply_lease.provider;
 unsigned generation=0;
 /* The MP-domain remap applies only to the provider's own verify result;
  * the pre-verify gate already speaks MFC errors. */
 if(!e){e=!b?I9100_MFC_OWNER:i9100_mfc_board_irq_verify(b,ctl,&generation);
  if(e==I9100_MP_BUSY)e=I9100_MFC_BUSY;else if(e)e=I9100_MFC_OWNER;}
 if(!e){n->ctl.token=ctl;n->ctl.board=b;n->ctl.generation=n->supply_lease.generation;
  n->ctl.irq_generation=generation;n->ctl.bound=1;}
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_control_drop(i9100_mfc_native *n,uintptr_t ctl)
{
 if(!n||!ctl)return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;
 int e=!n->ctl.bound||n->ctl.token!=ctl?I9100_MFC_OWNER:0;
 if(!e){n->ctl.bound=0;n->ctl.token=0;n->ctl.board=0;n->ctl.generation=n->ctl.irq_generation=0;}
 IOLockUnlock(n->lifecycle);return e;
}
static int start_impl(i9100_mfc_native *n,const i9100_mfc_firmware_profile *profile,const void *firmware,uint32_t bytes,
 const void *stream,uint32_t stream_bytes,uintptr_t ctl)
{
 if(!n||(stream==0)!=(stream_bytes==0U)||(n->decode_enabled&&!stream_bytes))return I9100_MFC_ARGUMENT;if(take(n->lifecycle))return I9100_MFC_BUSY;
 /* Qualification is verified before any hardware, polling or fault path:
  * a refused foreign/raw entry reports ownership without poisoning the
  * bound controller; after this point an OWNER from resources() is real
  * damage to the legal owner and still latches. */
 int e=control(n,ctl,I9100_MFC_OWNER);
 if(e){IOLockUnlock(n->lifecycle);return e;}
 e=n->phase!=I9100_MFC_N_PREPARED?I9100_MFC_BUSY:n->codec_frozen?I9100_MFC_OWNER:resources(n);
 if(!e)e=i9100_mfc_stage_firmware(&n->core,i9100_dma_native_buffer(n->allocation[0]),i9100_dma_native_buffer(n->allocation[1]),profile,firmware,bytes);
 if(!e&&stream_bytes)e=i9100_mfc_stage_h264(&n->core,stream,stream_bytes);
 if(!e)e=i9100_mfc_boot(&n->core);
 if(e&&e!=I9100_MFC_BUSY&&n->core.staged)fault(n,e);
 if(e&&e!=I9100_MFC_BUSY&&e!=I9100_MFC_ARGUMENT)fault(n,e);
 if(!e){n->phase=I9100_MFC_N_BOOT;n->stream_supplied=stream_bytes!=0U;}IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_start(i9100_mfc_native *n,const i9100_mfc_firmware_profile *profile,const void *firmware,uint32_t bytes,
 const void *stream,uint32_t stream_bytes)
{return start_impl(n,profile,firmware,bytes,stream,stream_bytes,0);}
int i9100_mfc_native_start_ctl(i9100_mfc_native *n,const i9100_mfc_firmware_profile *profile,const void *firmware,uint32_t bytes,
 const void *stream,uint32_t stream_bytes,uintptr_t ctl)
{return start_impl(n,profile,firmware,bytes,stream,stream_bytes,ctl);}
static int poll_impl(i9100_mfc_native *n,uintptr_t ctl)
{
 if(!n)return I9100_MFC_ARGUMENT;if(take(n->lifecycle))return I9100_MFC_BUSY;
 int e=n->error?n->error:control(n,ctl,I9100_MFC_BUSY);
 /* A latched error or a refused qualification exits before hardware and
  * before fault(): a wrong controller can never damage the legal owner. */
 if(e)goto out;
 e=resources(n);
 /* READY skips the core poll only when no mailbox residue exists: a late
  * reply for a dead command still has to be drained through the real
  * polling path so request_ready can prove mailbox quiescence. */
 if(!e&&n->phase==I9100_MFC_N_READY&&!n->core.pending)goto out;
 /* A still-pending mailbox command is itself permission to poll: residue
  * must drain whatever phase the bookkeeping last reached. */
 if(!e&&!n->core.pending&&(n->phase<I9100_MFC_N_BOOT||n->phase>I9100_MFC_N_HEADER)&&
    n->phase!=I9100_MFC_N_BUFFERS&&n->phase!=I9100_MFC_N_FRAME){e=I9100_MFC_BUSY;goto out;}
 if(!e)e=i9100_mfc_poll(&n->core);
 /* A frozen codec drains an in-flight reply for mailbox evidence but never
  * advances the command ladder: BOOT/SYS_INIT/OPEN/HEADER/INIT_BUFFERS
  * issue is refused here and FRAME/LAST is refused in decode_ready. */
 if(!e&&!n->core.pending&&!n->codec_frozen){
  if(n->phase==I9100_MFC_N_BOOT){e=i9100_mfc_sys_init(&n->core);if(!e)n->phase=I9100_MFC_N_SYSINIT;}
  else if(n->phase==I9100_MFC_N_SYSINIT){
   if(n->stream_supplied){e=i9100_mfc_open_h264(&n->core);if(!e)n->phase=I9100_MFC_N_OPEN;}else n->phase=I9100_MFC_N_READY;
  }else if(n->phase==I9100_MFC_N_OPEN){e=i9100_mfc_parse_h264_header(&n->core);if(!e)n->phase=I9100_MFC_N_HEADER;}
  else if(n->phase==I9100_MFC_N_HEADER){
   if(n->decode_enabled){
    i9100_mfc_decode_io io={n,normal_nc};e=i9100_mfc_decoder_bind(1,&n->core,&n->decoder,&io);
    if(!e)e=i9100_mfc_decoder_init_buffers(&n->core);if(!e)n->phase=I9100_MFC_N_BUFFERS;
   }else n->phase=I9100_MFC_N_READY;
  }else if(n->phase==I9100_MFC_N_BUFFERS||n->phase==I9100_MFC_N_FRAME)n->phase=I9100_MFC_N_READY;
 }
 /* Frozen mid-pipeline is the reported end state, not a continuation in
  * progress: mailbox quiescence returns cleanly while the phase stays
  * exactly where the fault declaration caught it. */
 if(e&&e!=I9100_MFC_BUSY)fault(n,e);else if(!e&&n->phase!=I9100_MFC_N_READY&&!n->codec_frozen)e=I9100_MFC_BUSY;
out:IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_poll(i9100_mfc_native *n){return poll_impl(n,0);}
int i9100_mfc_native_poll_ctl(i9100_mfc_native *n,uintptr_t ctl){return poll_impl(n,ctl);}
int i9100_mfc_native_status(i9100_mfc_native *n,struct i9100_mfc_native_status *out)
{
 if(!n||!out)return I9100_MFC_ARGUMENT;if(take(n->lifecycle))return I9100_MFC_BUSY;memset(out,0,sizeof(*out));
 out->phase=n->phase;out->generation=n->supply_lease.generation;out->pending=n->core.pending;out->expected=n->core.expected;
 out->last_reply=n->core.last_reply;out->raw_interrupt=n->core.raw_interrupt;out->firmware_version=n->core.firmware_version;
 out->system_memory_bytes=n->core.sys_memory_bytes;out->instance=n->core.instance;out->instance_open=n->core.instance_open;
 out->width=n->core.sequence.width;out->height=n->core.sequence.height;out->dpb_count=n->core.sequence.dpb_count;out->consumed_bytes=n->core.consumed_bytes;
 out->bank_a_state=n->allocation[0]?i9100_dma_native_buffer(n->allocation[0])->state:0;
 out->bank_b_state=n->allocation[1]?i9100_dma_native_buffer(n->allocation[1])->state:0;
 out->held_mask=n->core.held_mask;out->error=n->error;out->cleanup_error=n->cleanup_error;out->stop_stage=n->core.stop_stage;
 out->decode_enabled=n->decode_enabled;out->frame_submitted=n->decoder.frame_submitted;out->frame_completed=n->decoder.frame_completed;
 out->display_ready=n->decoder.display_ready;out->leased=n->decoder.leased;out->input_bytes=n->decoder.input_bytes;
 out->input_start=n->decoder.input_start;out->draining=n->decoder.draining;out->drained=n->decoder.drained;
 out->raw_display=n->decoder.raw_display;out->raw_decoded=n->decoder.raw_decoded;out->raw_type=n->decoder.raw_type;
 out->raw_display_type=n->decoder.raw_display_type;out->raw_consumed=n->decoder.raw_consumed;
 out->codec_frozen=n->codec_frozen;
 IOLockUnlock(n->lifecycle);return 0;
}
static bool outside_context(i9100_mfc_native *n,const void *p,uint32_t bytes)
{uintptr_t a=(uintptr_t)p,b=(uintptr_t)n;return p&&bytes&&a<=~(uintptr_t)0-bytes&&!(a<b+sizeof(*n)&&b<a+bytes);}
static int decode_ready(i9100_mfc_native *n)
{
 if(!n->decode_enabled)return I9100_MFC_DISABLED;if(n->error)return n->error;
 if(n->codec_frozen)return I9100_MFC_OWNER;
 if(n->phase!=I9100_MFC_N_READY)return I9100_MFC_BUSY;
 int e=resources(n);if(e&&e!=I9100_MFC_BUSY)fault(n,e);return e;
}
static int frame_impl(i9100_mfc_native *n,const void *data,uint32_t bytes,uint32_t tag,int last,uintptr_t ctl)
{
 if(!n||(bytes&&!outside_context(n,data,bytes)))return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;int e=control(n,ctl,I9100_MFC_OWNER);
 if(e){IOLockUnlock(n->lifecycle);return e;} /* refused before hardware/fault */
 e=decode_ready(n);
 if(!e)e=i9100_mfc_decoder_frame(&n->core,data,bytes,tag,last);
 if(!e)n->phase=I9100_MFC_N_FRAME;else if(n->core.fault)fault(n,e);
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_frame(i9100_mfc_native *n,const void *data,uint32_t bytes,uint32_t tag,int last)
{return frame_impl(n,data,bytes,tag,last,0);}
int i9100_mfc_native_frame_ctl(i9100_mfc_native *n,const void *data,uint32_t bytes,uint32_t tag,int last,uintptr_t ctl)
{return frame_impl(n,data,bytes,tag,last,ctl);}
static int acquire_impl(i9100_mfc_native *n,i9100_mfc_frame_lease *lease,uintptr_t ctl)
{
 if(!n||!outside_context(n,lease,sizeof(*lease)))return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;int e=control(n,ctl,I9100_MFC_OWNER);
 if(e){IOLockUnlock(n->lifecycle);return e;} /* refused before hardware/fault */
 e=decode_ready(n);
 if(!e)e=i9100_mfc_decoder_acquire(&n->core,lease);IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_acquire(i9100_mfc_native *n,i9100_mfc_frame_lease *lease){return acquire_impl(n,lease,0);}
int i9100_mfc_native_acquire_ctl(i9100_mfc_native *n,i9100_mfc_frame_lease *lease,uintptr_t ctl){return acquire_impl(n,lease,ctl);}
int i9100_mfc_native_release(i9100_mfc_native *n,i9100_mfc_frame_lease *lease)
{
 if(!n||!outside_context(n,lease,sizeof(*lease)))return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;
 /* Release is a software ownership operation: no new hardware access and
  * still available when reset, power validation or close has failed. */
 int e=!n->decode_enabled?I9100_MFC_DISABLED:i9100_mfc_decoder_release(&n->core,lease);
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_copy_nv12(i9100_mfc_native *n,const i9100_mfc_frame_lease *lease,
 void *y,uint32_t yc,uint32_t ys,void *c,uint32_t cc,uint32_t cs)
{
 if(!n||!outside_context(n,lease,sizeof(*lease))||!outside_context(n,y,yc)||!outside_context(n,c,cc))return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;int e=decode_ready(n);
 if(!e)e=i9100_mfc_decoder_copy_nv12(&n->core,lease,y,yc,ys,c,cc,cs);
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_hold_get(i9100_mfc_native *n,uintptr_t token)
{
 if(!n||!token||token==(uintptr_t)n)return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;
 int e=n->phase==I9100_MFC_N_CLOSING||n->error?I9100_MFC_OWNER:0;
 if(!e&&n->holds>=4U)e=I9100_MFC_BUSY;
 if(!e)for(unsigned i=0;i<n->holds;i++)if(n->hold_tokens[i]==token){e=I9100_MFC_OWNER;break;}
 if(!e){n->hold_tokens[n->holds++]=token;}IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_hold_put(i9100_mfc_native *n,uintptr_t token)
{
 if(!n||!token)return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;
 int e=I9100_MFC_OWNER;
 for(unsigned i=0;i<n->holds;i++)if(n->hold_tokens[i]==token){n->hold_tokens[i]=n->hold_tokens[--n->holds];n->hold_tokens[n->holds]=0;e=0;break;}
 IOLockUnlock(n->lifecycle);return e;
}
/* Owned engine shutdown for the bound controller: the control lease proves
 * identity, resources() re-verifies supply/power/IRQ accounting, the core
 * stop performs the real reset/MC-wait/mailbox quiesce, and an independent
 * readback confirms RISC/MC/mailboxes/latch and both MMU domains are
 * actually quiet. Refused qualification exits before any hardware write; a
 * failed stop keeps the lease, arenas, IRQ and frame-return rights so the
 * caller can fix the cause and retry the same stage. */
int i9100_mfc_native_shutdown_ctl(i9100_mfc_native *n,uintptr_t ctl)
{
 if(!n)return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;
 int e=control(n,ctl,I9100_MFC_OWNER);
 if(!e)e=resources(n);
 if(!e)e=i9100_mfc_stop(&n->core);
 if(!e){
  /* Success requires more than the core's own bookkeeping: reread the
   * actual RISC/MC/mailbox/latch and both MMU domains. */
  uint32_t reset=rd(n,MN_MFC,0),mc=rd(n,MN_MFC,0x510);
  if(reset==0xffffffffU||mc==0xffffffffU)e=I9100_MFC_IO;
  else if((reset&1U)||(mc&3U)||rd(n,MN_MFC,0x30)||rd(n,MN_MFC,0x44)||rd(n,MN_MFC,8))e=I9100_MFC_BUSY;
  else for(unsigned unit=MN_MMU_L;unit<=MN_MMU_R;unit++)if(rd(n,unit,0)||rd(n,unit,0x18)){e=I9100_MFC_BUSY;break;}
 }
 if(e)n->cleanup_error=e;
 IOLockUnlock(n->lifecycle);return e;
}
/* The exact bound owner's declared fault freezes codec stepping: the
 * mailbox may still drain in-flight replies for retirement evidence and
 * the shutdown/stop/close path still proves quiescence, but the command
 * ladder, decode issue and boot entry all refuse from this point on.
 * Pure bookkeeping after the same control() identity proof every other
 * ctl entry uses — idempotent, callable while the domain is dead or
 * restored, and a refused foreign token changes nothing. */
int i9100_mfc_native_freeze_ctl(i9100_mfc_native *n,uintptr_t ctl)
{
 if(!n||!ctl)return I9100_MFC_ARGUMENT;
 if(take(n->lifecycle))return I9100_MFC_BUSY;
 int e=control(n,ctl,I9100_MFC_OWNER);
 if(!e)n->codec_frozen=1;
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_mfc_native_close(i9100_mfc_native **owner)
{
 if(!owner||!*owner)return I9100_MFC_ARGUMENT;i9100_mfc_native *n=*owner;
 if(n->lifecycle&&take(n->lifecycle))return I9100_MFC_BUSY;
 if(n->holds||n->ctl.bound){if(n->lifecycle)IOLockUnlock(n->lifecycle);return I9100_MFC_BUSY;}
 n->phase=I9100_MFC_N_CLOSING;
 int e=cleanup(n);n->cleanup_error=e;if(n->lifecycle)IOLockUnlock(n->lifecycle);
 if(e)return e;*owner=0;dispose(n);return 0;
}
