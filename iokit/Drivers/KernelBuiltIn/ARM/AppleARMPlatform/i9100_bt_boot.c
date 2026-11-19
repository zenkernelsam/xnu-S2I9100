/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bt_boot.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int hash_present(const uint8_t *p){unsigned bits=0;for(unsigned n=0;n<32;n++)bits|=p[n];return bits!=0;}
static int overlaps(const void *a,size_t an,const void *c,size_t cn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)c;return x>UINTPTR_MAX-an||y>UINTPTR_MAX-cn||(x+an>y&&y+cn>x);}
static int address_valid(const uint8_t *p)
{unsigned zero=0,ones=0;for(unsigned n=0;n<6;n++){zero|=p[n];ones|=p[n]^255U;}
 const uint8_t factory_default[6]={0,0,0,0xb1,0x30,0x43};return zero&&ones&&memcmp(p,factory_default,6);}
static int profile_valid(const struct i9100_bt_boot_profile *p)
{return p&&p->board_data_reviewed==1U&&p->ram_bytes&&p->ram_bytes<=16U*1024U*1024U&&
 (uint64_t)p->ram_base+p->ram_bytes<=0x100000000ULL&&p->timeout_ms>=5000U&&p->timeout_ms<=300000U&&
 p->rom.manufacturer==15U&&p->patched.manufacturer==15U&&hash_present(p->hcd_sha256)&&address_valid(p->factory_bdaddr_hci);}
static int record_valid(const uint8_t *p,uint32_t bytes,const struct i9100_bt_boot_profile *profile,unsigned count,int *wrote,int *launch)
{
 if(bytes<3U||bytes!=3U+p[2]||*launch)return I9100_BT_BOOT_LAYOUT;
 uint16_t op=u16(p);unsigned length=p[2];
 if(op==0xfc2eU){if(length||count)return I9100_BT_BOOT_LAYOUT;}
 else if(op==0xfc4cU){
  if(length<=4U)return I9100_BT_BOOT_LAYOUT;uint32_t address=u32(p+3);
  if(address<profile->ram_base||(uint64_t)address+length-4U>(uint64_t)profile->ram_base+profile->ram_bytes)return I9100_BT_BOOT_LAYOUT;
  *wrote=1;
 }else if(op==0xfc4eU){
  if(length!=4U||!*wrote||u32(p+3)!=profile->launch_address)return I9100_BT_BOOT_LAYOUT;
  uint32_t address=u32(p+3)&~1U;
  if(profile->launch_address!=0xffffffffU&&(address<profile->ram_base||(uint64_t)address>=(uint64_t)profile->ram_base+profile->ram_bytes))return I9100_BT_BOOT_LAYOUT;
  *launch=1;
 }else return I9100_BT_BOOT_LAYOUT;
 return 0;
}
int i9100_bt_hcd_plan(const uint8_t *file,uint32_t bytes,const struct i9100_bt_boot_profile *p,uint32_t *records)
{
 if(!file||!records||!bytes||bytes>I9100_BT_HCD_MAX||!profile_valid(p)||
    overlaps(file,bytes,records,sizeof(*records))||overlaps(p,sizeof(*p),records,sizeof(*records)))return I9100_BT_BOOT_ARGUMENT;
 *records=0;uint32_t at=0,count=0;int wrote=0,launched=0;
 while(at<bytes){
  if(bytes-at<3U)return I9100_BT_BOOT_LAYOUT;uint32_t amount=3U+file[at+2];
  if(amount>bytes-at||count==65536U)return I9100_BT_BOOT_LAYOUT;
  int e=record_valid(file+at,amount,p,count,&wrote,&launched);if(e)return e;
  count++;at+=amount;
 }
 if(!launched||!wrote)return I9100_BT_BOOT_LAYOUT;*records=count;return 0;
}
int i9100_bt_hcd_audit(const uint8_t *file,uint32_t bytes,const struct i9100_bt_boot_profile *p,
 int (*sha)(void *,const void *,uint32_t,uint8_t[32]),void *cookie,struct i9100_bt_hcd_record *index,uint32_t capacity,uint32_t *records)
{
 if(!sha||!index||!records||!capacity||capacity>65536U||
    overlaps(file,bytes,index,(size_t)capacity*sizeof(*index))||overlaps(index,(size_t)capacity*sizeof(*index),records,sizeof(*records))||
    overlaps(file,bytes,records,sizeof(*records))||overlaps(p,sizeof(*p),index,(size_t)capacity*sizeof(*index))||
    overlaps(p,sizeof(*p),records,sizeof(*records)))return I9100_BT_BOOT_ARGUMENT;
 *records=0;uint32_t count=0;
 int e=i9100_bt_hcd_plan(file,bytes,p,&count);if(e)return e;if(capacity<count)return I9100_BT_BOOT_ARGUMENT;
 uint8_t digest[32]={0};if(sha(cookie,file,bytes,digest))return I9100_BT_BOOT_HASH;
 if(memcmp(digest,p->hcd_sha256,32))return I9100_BT_BOOT_HASH;
 uint32_t at=0;for(unsigned n=0;n<count;n++){
  struct i9100_bt_hcd_record r;memset(&r,0,sizeof(r));r.offset=at;r.bytes=3U+file[at+2];r.opcode=u16(file+at);r.payload_bytes=file[at+2];
  if(sha(cookie,file+at,r.bytes,r.sha256))return I9100_BT_BOOT_HASH;index[n]=r;at+=r.bytes;
 }
 *records=count;return 0;
}
static int valid(const struct i9100_bt_boot *b)
{return b&&b->status.initialized&&b->self==(uintptr_t)b;}
static int fail(struct i9100_bt_boot *b,int e)
{b->status.fault=1;b->status.ready=0;b->status.phase=I9100_BT_BOOT_FAILED;if(!b->status.error)b->status.error=e;return e;}
static int verify(struct i9100_bt_boot *b)
{
 if(b->io.verify_link(b->io.cookie,b->status.generation))return fail(b,I9100_BT_BOOT_OWNER);
 uint64_t at=b->io.h4.now_ns(b->io.h4.cookie);
 if(at<b->status.last_ns)return fail(b,I9100_BT_BOOT_CLOCK);
 b->status.last_ns=at;
 if(!b->status.ready&&b->status.phase!=I9100_BT_BOOT_PREPARED&&at-b->status.started_ns>(uint64_t)b->profile.timeout_ms*1000000ULL)return fail(b,I9100_BT_BOOT_TIMEOUT);
 return 0;
}
static int same_time_link(struct i9100_bt_boot *b)
{return b->io.verify_link(b->io.cookie,b->status.generation)?fail(b,I9100_BT_BOOT_OWNER):0;}
static int guarded_lock(void *cookie)
{struct i9100_bt_boot *b=(struct i9100_bt_boot *)cookie;
 if(!b->status.closed&&!b->status.fault&&b->status.phase!=I9100_BT_BOOT_FAILED&&same_time_link(b))return -1;
 return b->io.h4.lock(b->io.h4.cookie);}
static void guarded_unlock(void *cookie)
{struct i9100_bt_boot *b=(struct i9100_bt_boot *)cookie;b->io.h4.unlock(b->io.h4.cookie);}
static int guarded_write(void *cookie,const uint8_t *p,unsigned bytes,unsigned *sent)
{struct i9100_bt_boot *b=(struct i9100_bt_boot *)cookie;return b->io.h4.write(b->io.h4.cookie,p,bytes,sent);}
static int guarded_quiesce(void *cookie)
{struct i9100_bt_boot *b=(struct i9100_bt_boot *)cookie;return b->io.h4.quiesce(b->io.h4.cookie);}
static uint64_t guarded_time(void *cookie)
{struct i9100_bt_boot *b=(struct i9100_bt_boot *)cookie;return b->io.h4.now_ns(b->io.h4.cookie);}
static int send(struct i9100_bt_boot *b,uint32_t phase,uint16_t op,const uint8_t *p,unsigned length)
{
 int state=verify(b);if(state)return state;
 if(b->h4.pending||b->deferred_opcode)return fail(b,I9100_BT_BOOT_BUSY);
 if(!b->h4.credits){b->deferred_phase=phase;b->deferred_opcode=op;b->deferred_bytes=length;
  if(length)memcpy(b->deferred_payload,p,length);b->credit_started=b->status.last_ns;return 0;}
 b->status.phase=phase;b->status.pending_opcode=op;
 int e=i9100_bt_command(&b->h4,op,p,length);return e?fail(b,e):0;
}
static int next_record(struct i9100_bt_boot *b)
{
 uint32_t n=b->status.record_index;if(n>=b->status.records)return fail(b,I9100_BT_BOOT_LAYOUT);
 const struct i9100_bt_hcd_record *r=&b->index[n];
 if(r->offset!=b->record_cursor||r->offset>=b->hcd_bytes||r->bytes<3U||r->bytes>b->hcd_bytes-r->offset||r->bytes!=3U+r->payload_bytes||r->reserved)return fail(b,I9100_BT_BOOT_LAYOUT);
 const uint8_t *data=b->hcd+r->offset;int wrote=1,launched=0;
 if(r->opcode!=u16(data)||r->payload_bytes!=data[2]||record_valid(data,r->bytes,&b->profile,n,&wrote,&launched))return fail(b,I9100_BT_BOOT_LAYOUT);
 uint8_t digest[32]={0};if(b->io.sha256(b->io.cookie,data,r->bytes,digest)||memcmp(digest,r->sha256,32))return fail(b,I9100_BT_BOOT_HASH);
 if(r->opcode==0xfc2eU)return fail(b,I9100_BT_BOOT_LAYOUT);
 b->current_record_bytes=r->bytes;
 if(r->opcode==0xfc4eU){if(n+1U!=b->status.records)return fail(b,I9100_BT_BOOT_LAYOUT);b->status.launch_possible=1;}
 return send(b,I9100_BT_BOOT_RECORD,r->opcode,data+3,r->payload_bytes);
}
static void delay(struct i9100_bt_boot *b,uint32_t phase,uint32_t ms)
{b->status.phase=phase;b->status.delay_until_ns=b->status.last_ns+(uint64_t)ms*1000000ULL;}
static struct i9100_bt_version version(const uint8_t *p)
{struct i9100_bt_version v={p[1],p[4],u16(p+2),u16(p+5),u16(p+7)};return v;}
static int same_version(const struct i9100_bt_version *a,const struct i9100_bt_version *b)
{return a->hci_version==b->hci_version&&a->lmp_version==b->lmp_version&&a->hci_revision==b->hci_revision&&a->manufacturer==b->manufacturer&&a->lmp_subversion==b->lmp_subversion;}
static int complete(struct i9100_bt_boot *b,const struct i9100_bt_frame *f)
{
 int state=verify(b);if(state)return state;
 if(f->type!=4U||f->length<6U||f->bytes[0]!=0x0eU||f->bytes[1]+2U!=f->length||
    u16(f->bytes+3)!=b->status.pending_opcode||f->bytes[5])return fail(b,I9100_BT_FRAME);
 const uint8_t *rp=f->bytes+5;unsigned bytes=f->length-5U;uint32_t phase=b->status.phase;
 b->status.pending_opcode=0;
 switch(phase){
 case I9100_BT_BOOT_RESET:
 case I9100_BT_BOOT_POST_RESET:
  if(bytes!=1U)return fail(b,I9100_BT_FRAME);
  if(b->status.last_ns>UINT64_MAX-250000000ULL)return fail(b,I9100_BT_BOOT_CLOCK);
  delay(b,phase==I9100_BT_BOOT_RESET?I9100_BT_BOOT_RESET_DELAY:I9100_BT_BOOT_POST_RESET_DELAY,100);return 0;
 case I9100_BT_BOOT_ROM_VERSION:
  if(bytes!=9U)return fail(b,I9100_BT_FRAME);b->status.observed_rom=version(rp);
  if(!same_version(&b->status.observed_rom,&b->profile.rom))return fail(b,I9100_BT_BOOT_ID);
  b->status.mutation_possible=1;return send(b,I9100_BT_BOOT_MINIDRV,0xfc2eU,0,0);
 case I9100_BT_BOOT_MINIDRV:
  if(bytes!=1U)return fail(b,I9100_BT_FRAME);if(b->optional_minidrv)b->status.confirmed_records++;
  if(b->status.last_ns>UINT64_MAX-250000000ULL)return fail(b,I9100_BT_BOOT_CLOCK);
  delay(b,I9100_BT_BOOT_MINIDRV_DELAY,50);return 0;
 case I9100_BT_BOOT_RECORD:
  if(bytes!=1U)return fail(b,I9100_BT_FRAME);
  b->record_cursor+=b->current_record_bytes;b->status.confirmed_records++;b->status.record_index++;
  if(b->status.launch_possible){if(b->status.record_index!=b->status.records||b->record_cursor!=b->hcd_bytes)return fail(b,I9100_BT_BOOT_LAYOUT);
   if(b->status.last_ns>UINT64_MAX-250000000ULL)return fail(b,I9100_BT_BOOT_CLOCK);delay(b,I9100_BT_BOOT_LAUNCH_DELAY,250);return 0;}
  return next_record(b);
 case I9100_BT_BOOT_VERSION:
  if(bytes!=9U)return fail(b,I9100_BT_FRAME);b->status.observed_patched=version(rp);
  if(!same_version(&b->status.observed_patched,&b->profile.patched))return fail(b,I9100_BT_BOOT_ID);
  return send(b,I9100_BT_BOOT_ADDRESS,0x1009U,0,0);
 case I9100_BT_BOOT_ADDRESS:
  if(bytes!=7U)return fail(b,I9100_BT_FRAME);
  if(!address_valid(rp+1)||memcmp(rp+1,b->profile.factory_bdaddr_hci,6))return fail(b,I9100_BT_BOOT_ID);
  memcpy(b->status.address,rp+1,6);return send(b,I9100_BT_BOOT_BUFFER,0x1005U,0,0);
 case I9100_BT_BOOT_BUFFER:
  if(bytes!=8U)return fail(b,I9100_BT_FRAME);
  b->status.acl_mtu=u16(rp+1);b->status.sco_mtu=rp[3];b->status.acl_packets=u16(rp+4);b->status.sco_packets=u16(rp+6);
  if(!b->status.acl_mtu||b->status.acl_mtu>4096U||!b->status.acl_packets)return fail(b,I9100_BT_BOOT_ID);
  return send(b,I9100_BT_BOOT_FEATURES,0x1003U,0,0);
 case I9100_BT_BOOT_FEATURES:
  if(bytes!=9U)return fail(b,I9100_BT_FRAME);memcpy(b->status.features,rp+1,8);
  if(b->status.features[4]&0x40U)return send(b,I9100_BT_BOOT_LE_BUFFER,0x2002U,0,0);
  {uint8_t off=0;return send(b,I9100_BT_BOOT_SCAN_OFF,0x0c1aU,&off,1);}
 case I9100_BT_BOOT_LE_BUFFER:
  if(bytes!=4U)return fail(b,I9100_BT_FRAME);b->status.le_mtu=u16(rp+1);b->status.le_packets=rp[3];
  if((!b->status.le_mtu)!=(!b->status.le_packets)||b->status.le_mtu>4096U)return fail(b,I9100_BT_BOOT_ID);
  b->status.le_buffers_valid=1;
  {uint8_t off=0;return send(b,I9100_BT_BOOT_SCAN_OFF,0x0c1aU,&off,1);}
 case I9100_BT_BOOT_SCAN_OFF:
  if(bytes!=1U)return fail(b,I9100_BT_FRAME);b->status.ready=1;b->status.phase=I9100_BT_BOOT_READY;return 0;
 default:return fail(b,I9100_BT_UNMATCHED);
 }
}
int i9100_bt_boot_init(struct i9100_bt_boot *b,int enabled,const struct i9100_bt_boot_io *io,
 const struct i9100_bt_boot_profile *p,const uint8_t *hcd,uint32_t bytes,const struct i9100_bt_hcd_record *index,uint32_t count,uint32_t generation)
{
 if(!b||b->status.initialized)return I9100_BT_BOOT_ARGUMENT;if(enabled!=1)return I9100_BT_BOOT_DISABLED;
 if(!io||!io->read||!io->verify_link||!io->sha256||!io->receive||!io->h4.lock||!io->h4.unlock||
    !io->h4.write||!io->h4.quiesce||!io->h4.now_ns||!index||!generation)return I9100_BT_BOOT_ARGUMENT;
 if(overlaps(b,sizeof(*b),io,sizeof(*io))||overlaps(b,sizeof(*b),p,sizeof(*p))||
    count>65536U||overlaps(b,sizeof(*b),hcd,bytes)||overlaps(b,sizeof(*b),index,(size_t)count*sizeof(*index)))return I9100_BT_BOOT_ARGUMENT;
 uint32_t records=0;int e=i9100_bt_hcd_plan(hcd,bytes,p,&records);if(e)return e;if(records!=count)return I9100_BT_BOOT_LAYOUT;
 uint8_t digest[32]={0};if(io->sha256(io->cookie,hcd,bytes,digest)||memcmp(digest,p->hcd_sha256,32))return I9100_BT_BOOT_HASH;
 if(io->verify_link(io->cookie,generation))return I9100_BT_BOOT_OWNER;
 uint32_t at=0;
 for(unsigned n=0;n<count;n++){
  const struct i9100_bt_hcd_record *r=&index[n];uint32_t length=3U+hcd[at+2];
  if(r->offset!=at||r->bytes!=length||r->opcode!=u16(hcd+at)||r->payload_bytes!=hcd[at+2]||r->reserved)return I9100_BT_BOOT_LAYOUT;
  if(io->sha256(io->cookie,hcd+at,length,digest)||memcmp(digest,r->sha256,32))return I9100_BT_BOOT_HASH;
  at+=length;
 }
 memset(b,0,sizeof(*b));b->profile=*p;b->io=*io;b->hcd=hcd;b->hcd_bytes=bytes;b->index=index;b->self=(uintptr_t)b;
 b->status.initialized=b->status.enabled=1;b->status.records=records;b->status.generation=generation;b->status.phase=I9100_BT_BOOT_PREPARED;
 b->optional_minidrv=u16(hcd)==0xfc2eU;b->status.record_index=b->optional_minidrv;
 b->record_cursor=b->optional_minidrv?3U:0U;
 struct i9100_bt_io guarded={b,guarded_lock,guarded_unlock,guarded_write,guarded_quiesce,guarded_time};
 e=i9100_bt_init(&b->h4,&guarded);if(!e)e=i9100_bt_enable(&b->h4,1,generation);if(e)return fail(b,e);return 0;
}
int i9100_bt_boot_start(struct i9100_bt_boot *b)
{
 if(!valid(b))return I9100_BT_BOOT_ARGUMENT;
 if(!b->status.enabled||b->status.phase!=I9100_BT_BOOT_PREPARED||b->status.fault)return I9100_BT_BOOT_BUSY;
 int e=verify(b);if(e)return e;unsigned stale=0;
 if(b->io.read(b->io.cookie,b->rx,sizeof(b->rx),&stale))return fail(b,I9100_BT_BOOT_IO);
 if(stale)return fail(b,I9100_BT_FRAME); /* No fake synchronization by flushing. */
 b->status.started_ns=b->status.last_ns;return send(b,I9100_BT_BOOT_RESET,0x0c03U,0,0);
}
static int advance_delay(struct i9100_bt_boot *b)
{
 if(b->status.last_ns<b->status.delay_until_ns)return 0;
 switch(b->status.phase){
 case I9100_BT_BOOT_RESET_DELAY:return send(b,I9100_BT_BOOT_ROM_VERSION,0x1001U,0,0);
 case I9100_BT_BOOT_MINIDRV_DELAY:return next_record(b);
 case I9100_BT_BOOT_LAUNCH_DELAY:return send(b,I9100_BT_BOOT_POST_RESET,0x0c03U,0,0);
 case I9100_BT_BOOT_POST_RESET_DELAY:return send(b,I9100_BT_BOOT_VERSION,0x1001U,0,0);
 default:return 0;
 }
}
int i9100_bt_boot_step(struct i9100_bt_boot *b,unsigned budget)
{
 if(!valid(b)||!budget||budget>16U)return I9100_BT_BOOT_ARGUMENT;
 if(b->status.closed||!b->status.enabled)return I9100_BT_BOOT_DISABLED;
 if(b->status.fault)return I9100_BT_BOOT_FAULT;if(b->status.phase==I9100_BT_BOOT_PREPARED)return I9100_BT_BOOT_BUSY;
 uint64_t previous=b->status.last_ns;int e=verify(b);if(e)return e;
 if(b->status.last_ns==previous){if(++b->stalled_steps>=4096U)return fail(b,I9100_BT_BOOT_CLOCK);}else b->stalled_steps=0;
 e=i9100_bt_poll(&b->h4);if(e&&e!=I9100_BT_BUSY)return fail(b,e);
 unsigned frames=0,bytes_seen=0;
 while(frames<budget&&bytes_seen<1024U){
  if(b->rx_offset==b->rx_used){
   b->rx_offset=b->rx_used=0;unsigned got=0;
   e=b->io.read(b->io.cookie,b->rx,sizeof(b->rx),&got);if(e)return fail(b,I9100_BT_BOOT_IO);
   if(got>sizeof(b->rx))return fail(b,I9100_BT_FRAME);b->rx_used=got;if(!got)break;
  }
  struct i9100_bt_frame frame={0};unsigned consumed=0;
  e=i9100_bt_feed(&b->h4,b->rx+b->rx_offset,b->rx_used-b->rx_offset,&consumed,&frame);b->rx_offset+=consumed;bytes_seen+=consumed;
  if(!consumed||b->rx_offset>b->rx_used)return fail(b,I9100_BT_FRAME);
  if(e<0&&!(b->status.ready&&e==I9100_BT_CONTROLLER&&frame.bytes))return fail(b,e);
  if(!frame.bytes)continue;frames++;if(b->status.rx_packets!=UINT32_MAX)b->status.rx_packets++;
  if(b->status.ready){if(frame.type==4U&&frame.bytes[0]==0x10U)return fail(b,I9100_BT_CONTROLLER);
   if(e==I9100_BT_CONTROLLER){if(b->status.controller_errors!=UINT32_MAX)b->status.controller_errors++;}
   if(b->io.receive(b->io.cookie,&frame))return fail(b,I9100_BT_BOOT_IO);
   b->h4.last_status=0; /* valid per-command rejection belongs to the host stack */
  }
  else if(frame.type==4U&&frame.bytes[0]==0x0eU&&frame.length==5U&&!u16(frame.bytes+3)){
   /* HCI's opcode-zero COMMAND_COMPLETE only replenishes command credits. */
  }else if(frame.type==4U&&(frame.bytes[0]==0x0eU||frame.bytes[0]==0x0fU)){
   e=complete(b,&frame);if(e)return e;
  }else if(frame.type==4U&&frame.bytes[0]==0x10U)return fail(b,I9100_BT_CONTROLLER);
  else if(!b->status.ready&&(frame.type==2U||frame.type==3U))return fail(b,I9100_BT_FRAME);
  else if(b->io.receive(b->io.cookie,&frame))return fail(b,I9100_BT_BOOT_IO);
 }
 if(b->deferred_opcode){
  if(b->status.last_ns-b->credit_started>1000000000ULL)return fail(b,I9100_BT_TIMEOUT);
  if(!b->h4.credits)return 0;
  uint16_t op=b->deferred_opcode;uint32_t phase=b->deferred_phase,bytes=b->deferred_bytes;
  b->deferred_opcode=0;return send(b,phase,op,b->deferred_payload,bytes);
 }
 return advance_delay(b);
}
int i9100_bt_boot_command(struct i9100_bt_boot *b,uint16_t op,const uint8_t *payload,unsigned bytes)
{
 if(!valid(b)||!op||!(op>>10)||(op>>10)==0x3fU||op==0x0c03U||op==0x0c35U||bytes>255U||(bytes&&(!payload||
    overlaps(b,sizeof(*b),payload,bytes)||overlaps(b->hcd,b->hcd_bytes,payload,bytes)||
    overlaps(b->index,(size_t)b->status.records*sizeof(*b->index),payload,bytes))))return I9100_BT_BOOT_ARGUMENT;
 if(!b->status.ready||b->status.closed||b->status.fault)return I9100_BT_BOOT_BUSY;
 int e=verify(b);if(e)return e;e=i9100_bt_command(&b->h4,op,payload,bytes);
 return e&&e!=I9100_BT_BUSY&&e!=I9100_BT_ARGUMENT?fail(b,e):e;
}
int i9100_bt_boot_stop(struct i9100_bt_boot *b)
{
 if(!valid(b))return I9100_BT_BOOT_ARGUMENT;if(b->status.closed)return 0;
 b->status.ready=0;b->status.phase=I9100_BT_BOOT_FAILED; /* only quiesce is admitted after this point */
 int e=b->h4.enabled?i9100_bt_enable(&b->h4,0,0):0;
 if(e){b->status.cleanup_error=e;return e;}
 b->status.enabled=b->status.ready=0;b->status.closed=1;b->status.phase=I9100_BT_BOOT_CLOSED;b->status.cleanup_error=0;return 0;
}
int i9100_bt_boot_acl_packet(struct i9100_bt_boot *b,const uint8_t *packet,unsigned bytes)
{
 if(!valid(b)||!packet||bytes<6U||bytes>4101U||overlaps(b,sizeof(*b),packet,bytes)||
    overlaps(b->hcd,b->hcd_bytes,packet,bytes)||overlaps(b->index,(size_t)b->status.records*sizeof(*b->index),packet,bytes))return I9100_BT_BOOT_ARGUMENT;
 if(!b->status.ready||b->status.closed||b->status.fault)return I9100_BT_BOOT_BUSY;
 int e=verify(b);if(e)return e;e=i9100_bt_acl_packet(&b->h4,packet,bytes);
 return e&&e!=I9100_BT_BUSY&&e!=I9100_BT_ARGUMENT?fail(b,e):e;
}
int i9100_bt_boot_status(const struct i9100_bt_boot *b,struct i9100_bt_boot_status *out)
{
 if(!valid(b)||!out)return I9100_BT_BOOT_ARGUMENT;uintptr_t a=(uintptr_t)b,p=(uintptr_t)out;
 if(a>UINTPTR_MAX-sizeof(*b)||p>UINTPTR_MAX-sizeof(*out)||(a+sizeof(*b)>p&&p+sizeof(*out)>a)||
    overlaps(b->hcd,b->hcd_bytes,out,sizeof(*out))||overlaps(b->index,(size_t)b->status.records*sizeof(*b->index),out,sizeof(*out)))return I9100_BT_BOOT_ARGUMENT;
 *out=b->status;out->pending_opcode=b->h4.pending?b->h4.opcode:b->deferred_opcode;return 0;
}
