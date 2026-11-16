/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_native_parent_model.h"
#include "i9100_sdio_register_model.h"

/* Only the SDHCI register boundary is replaced. Native orchestration, both
 * mutexes, time/delay, SDIO init/enable/CIS/function/CMD52/CMD53 and cleanup
 * execute production code. The IOKit maps and card remain stand-ins. */
namespace card_model = i9100_card_model;
static i9100_peripheral_native *expected_native;
static i9100_sdio_io original_io;
static unsigned wrapped_inits;

static void check_register_cookie(void *cookie)
{
 assert(expected_native&&cookie==&expected_native->sdio);
 assert(expected_native->sdio.controller->held&&card_model::m.locked);
}
static uint8_t card_read8(void *c,unsigned r)
{check_register_cookie(c);return card_model::r8(&card_model::m,r);}
static uint16_t card_read16(void *c,unsigned r)
{check_register_cookie(c);return card_model::r16(&card_model::m,r);}
static uint32_t card_read32(void *c,unsigned r)
{check_register_cookie(c);return card_model::r32(&card_model::m,r);}
static void card_write8(void *c,unsigned r,uint8_t v)
{check_register_cookie(c);card_model::w8(&card_model::m,r,v);}
static void card_write16(void *c,unsigned r,uint16_t v)
{check_register_cookie(c);card_model::w16(&card_model::m,r,v);}
static void card_write32(void *c,unsigned r,uint32_t v)
{check_register_cookie(c);card_model::w32(&card_model::m,r,v);}
static int card_lock(void *c)
{
 assert(expected_native&&c==original_io.cookie&&!card_model::m.locked);
 int e=original_io.lock(c);
 if(!e){assert(expected_native->sdio.controller->held);card_model::m.locked=1;}
 return e;
}
static void card_unlock(void *c)
{check_register_cookie(c);card_model::m.locked=0;original_io.unlock(c);}

extern "C" int __real_i9100_sdio_init(i9100_sdio *,const i9100_sdio_io *,uint32_t);
extern "C" int __wrap_i9100_sdio_init(i9100_sdio *s,const i9100_sdio_io *io,uint32_t clock)
{
 assert(expected_native&&s==&expected_native->sdio.card&&io->cookie==&expected_native->sdio);
 assert(expected_native->sdio.outer->held&&!expected_native->sdio.controller->held);
 assert(io->read8==sdio_read8&&io->read16==sdio_read16&&io->read32==sdio_read32);
 assert(io->write8==sdio_write8&&io->write16==sdio_write16&&io->write32==sdio_write32);
 assert(io->lock==sdio_lock&&io->unlock==sdio_unlock&&io->now_ns==time_ns&&io->delay_us==pause_us);
 original_io=*io;wrapped_inits++;
 i9100_sdio_io boundary=*io;
 boundary.read8=card_read8;boundary.read16=card_read16;boundary.read32=card_read32;
 boundary.write8=card_write8;boundary.write16=card_write16;boundary.write32=card_write32;
 boundary.lock=card_lock;boundary.unlock=card_unlock;
 return __real_i9100_sdio_init(s,&boundary,clock);
}

static const uint32_t saved_gpio[]={0xc0000900U,0x000000acU,0xb055aaaaU,0x9988bbbbU};
static const uint32_t saved_ip=0xa5010403U,saved_source=0x32108001U;
static i9100_peripheral_native *fresh()
{
 setup();card_model::reset();wrapped_inits=0;
 i9100_peripheral_native *c=parent();expected_native=c;
 const i9100_gpio_bank_config &bank=i9100_u1_banks[I9100_GPK3];
 for(unsigned n=0;n<4;n++)memory[i9100_gpio_physical[bank.region]][(bank.offset+n*4U)/4]=saved_gpio[n];
 memory[0x10030000U][0xc940/4]=saved_ip;memory[0x10030000U][0xc340/4]=saved_source;
 return c;
}
static int open(i9100_peripheral_native *c,i9100_native_transport_lease *lease,i9100_sdio **card,i9100_f2_io *io)
{return i9100_peripheral_native_sdio_open_owned(c,lease,48000000U,1,card,io);}
static void restored(i9100_peripheral_native *c)
{
 assert(!c->sdio.live&&!c->sdio.closing&&!c->sdio.owner&&!c->sdio.borrowers);
 assert(!c->sdio.pins.active&&!c->sdio.ip_gate.active&&!c->sdio.source_gate.active);
 const i9100_gpio_bank_config &bank=i9100_u1_banks[I9100_GPK3];
 for(unsigned n=0;n<4;n++)assert(memory[i9100_gpio_physical[bank.region]][(bank.offset+n*4U)/4]==saved_gpio[n]);
 assert(memory[0x10030000U][0xc940/4]==saved_ip&&memory[0x10030000U][0xc340/4]==saved_source);
 assert(card_model::le32(card_model::m.regs+52)==0x11002200U);
 assert(card_model::le32(card_model::m.regs+56)==0x00400080U);
 assert(card_model::le32(card_model::m.regs+128)==0x12345a03U);
 assert(card_model::le32(card_model::m.regs+132)==card_model::m.original_feedback);
 assert(!card_model::m.regs[41]&&!card_model::le16(card_model::m.regs+44)&&!card_model::m.locked);
 assert(!memory.count(0x12530000U)&&!memory.count(0x12550000U)); /* no SD/eMMC controller */
 assert(!i9100_peripheral_native_destroy(&c)&&!c&&!active_allocations);expected_native=0;
}
static void enumeration_and_pio()
{
 i9100_peripheral_native *c=fresh();i9100_native_transport_lease lease={};i9100_sdio *card=0;i9100_f2_io io;
 assert(!open(c,&lease,&card,&io)&&wrapped_inits==1U&&card==&c->sdio.card);
 assert(lease.owned&&lease.self==(uintptr_t)&lease&&c->sdio.owner==&lease&&lease.generation==1U);
 assert(card->initialized&&card->active&&!card->fault&&card->host_owned&&card->functions==2U);
 assert(card->vendor==0x02d0&&card->device==0x4330&&card->rca==0x1234U&&card->revision==0x22);
 assert(card->clock_hz==375000U&&card->block_size[1]==64U&&!card->block_size[2]);
 assert(card->io.now_ns==original_io.now_ns&&card->io.delay_us==original_io.delay_us&&card->io.cookie==original_io.cookie);
 assert(io.cookie==&lease&&io.lock==owned_sdio_lock&&io.unlock==owned_sdio_unlock);
 assert(card_model::m.functions[0][2]==2U&&card_model::m.functions[0][3]==2U);
 assert(card_model::m.functions[0][0x110]==64U&&!card_model::m.functions[0][0x111]);
 assert(!card_model::m.functions[0][0x210]&&!card_model::m.functions[0][0x211]);
 assert(card_model::m.functions[0][7]==0xa0&&card_model::m.command_count[0]==1U);
 assert(card_model::m.command_count[5]==2U&&card_model::m.command_count[3]==1U&&card_model::m.command_count[7]==1U);
 assert(memory[0x11000000U][0xa0/4]==0xc2222922U);
 assert(memory[0x10030000U][0xc940/4]==(saved_ip|(1U<<8)));
 assert(memory[0x10030000U][0xc340/4]==(saved_source|(1U<<12)));
 assert(ns>=25000000ULL&&card_model::m.command_count[52]>10U);
 const unsigned sizes[]={1,3,64,511,512,576};
 for(unsigned k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
  unsigned n=sizes[k];uint8_t source[578],dest[578];memset(source,0x7a,sizeof(source));memset(dest,0xb6,sizeof(dest));
  for(unsigned j=0;j<n;j++)source[j+1]=(uint8_t)(j*13U+k);
  assert(!io.lock(io.cookie));
  assert(!i9100_sdio_cmd53(card,1,1,0x1800,1,source+1,n));
  assert(!i9100_sdio_cmd53(card,0,1,0x1800,1,dest+1,n));io.unlock(io.cookie);
  assert(!memcmp(source+1,dest+1,n)&&dest[0]==0xb6&&dest[n+1]==0xb6);
  assert(!memcmp(card_model::m.functions[1]+0x1800,source+1,n));
 }
 uint8_t v=0x96;assert(!io.lock(io.cookie));
 assert(!i9100_sdio_cmd52(card,1,1,0x12345,&v));v=0;
 assert(!i9100_sdio_cmd52(card,0,1,0x12345,&v)&&v==0x96);io.unlock(io.cookie);
 assert(card_model::m.command_count[53]==12U&&!card->fault&&!card->last_function);
 assert(!i9100_peripheral_native_transport_close(&lease)&&!lease.owned);
 assert(!card_model::m.functions[0][2]);restored(c);
}
static void holds_and_stale_callbacks()
{
 i9100_peripheral_native *c=fresh();i9100_native_transport_lease lease={};i9100_sdio *card=0;i9100_f2_io io;
 assert(!open(c,&lease,&card,&io));i9100_native_transport_hold session={},worker={};
 assert(!i9100_peripheral_native_transport_hold_get(&lease,&session));
 assert(!i9100_peripheral_native_transport_hold_get(&lease,&worker)&&c->sdio.borrowers==2U);
 unsigned commands=card_model::m.commands,writes=card_model::m.register_writes;
 assert(i9100_peripheral_native_transport_close(&lease)==I9100_SDIO_BUSY&&!c->sdio.closing);
 assert(card_model::m.commands==commands&&card_model::m.register_writes==writes);
 i9100_native_transport_lease copy=lease;i9100_f2_io copied=io;copied.cookie=&copy;
 assert(copied.lock(copied.cookie)&&i9100_peripheral_native_transport_close(&copy)==I9100_GPIO_OWNER);
 assert(!io.lock(io.cookie));assert(i9100_peripheral_native_transport_hold_put(&worker)==I9100_GPIO_BUSY);io.unlock(io.cookie);
 assert(!i9100_peripheral_native_transport_hold_put(&worker));
 assert(!i9100_peripheral_native_transport_hold_put(&session));
 i9100_f2_io old=io;uint32_t generation=lease.generation;
 assert(!i9100_peripheral_native_transport_close(&lease)&&old.lock(old.cookie));
 i9100_native_transport_lease next={};assert(!open(c,&next,&card,&io)&&next.generation>generation);
 commands=card_model::m.commands;assert(old.lock(old.cookie)&&copied.lock(copied.cookie));
 assert(card_model::m.commands==commands);
 assert(!io.lock(io.cookie));
 assert(!original_io.lock(original_io.cookie));uint8_t v=0;
 commands=card_model::m.commands;assert(i9100_sdio_cmd52(card,0,1,0,&v)==I9100_SDIO_BUSY);
 assert(card_model::m.commands==commands);original_io.unlock(original_io.cookie);io.unlock(io.cookie);
 assert(!i9100_peripheral_native_transport_close(&next));restored(c);
}
static void initialization_faults()
{
 for(unsigned fault=0;fault<9;fault++){
  i9100_peripheral_native *c=fresh();i9100_native_transport_lease lease={};i9100_sdio *card=0;i9100_f2_io io;
  int expected=I9100_SDIO_CARD;
  switch(fault){
   case 0:card_model::m.crc=1;expected=I9100_SDIO_CRC;break;
   case 1:card_model::m.r5_error=0x100;break;
   case 2:card_model::m.functions[0][0x10a]=0;break; /* null CIS pointer */
   case 3:card_model::m.functions[0][0x109]=0xff;card_model::m.functions[0][0x10a]=0xff;
          card_model::m.functions[0][0x10b]=1;card_model::m.functions[0][0x1ffff]=0x20;break;
   case 4:card_model::m.functions[0][0x1000]=0x20;card_model::m.functions[0][0x1001]=3;break;
   case 5:card_model::m.clock_stall=1;freeze_model_time=true;expected=I9100_SDIO_TIMEOUT;break;
   case 6:card_model::m.reset_stall=1;expected=I9100_SDIO_TIMEOUT;break;
   case 7:card_model::m.command_stall=1;expected=I9100_SDIO_TIMEOUT;break;
   case 8:card_model::m.no_ready=1;expected=I9100_SDIO_TIMEOUT;break;
  }
  assert(open(c,&lease,&card,&io)==expected&&wrapped_inits==1U);
  assert(!card&&!lease.owned&&!io.cookie&&!io.lock&&card_model::m.commands<200U);
  restored(c);
 }
}
static void transfer_fault_and_explicit_cleanup()
{
 for(unsigned fault=0;fault<2;fault++){
  i9100_peripheral_native *c=fresh();i9100_native_transport_lease lease={};i9100_sdio *card=0;i9100_f2_io io;
  assert(!open(c,&lease,&card,&io));i9100_native_transport_hold hold={};
  assert(!i9100_peripheral_native_transport_hold_get(&lease,&hold));
  if(fault)card_model::m.r5_error=0x100;else card_model::m.crc=1;
  uint8_t bytes[4]={};assert(!io.lock(io.cookie));
  assert(i9100_sdio_cmd53(card,0,1,0x1800,1,bytes,sizeof(bytes))==(fault?I9100_SDIO_CARD:I9100_SDIO_CRC));io.unlock(io.cookie);
  assert(card->fault&&i9100_peripheral_native_transport_hold_check(&hold)==I9100_GPIO_IO);
  unsigned commands=card_model::m.commands;uint8_t v=0;
  assert(i9100_sdio_cmd52(card,0,1,0,&v)==I9100_SDIO_IO&&commands==card_model::m.commands);
  assert(i9100_peripheral_native_transport_close(&lease)==I9100_SDIO_BUSY&&!c->sdio.closing);
  assert(!i9100_peripheral_native_transport_hold_put(&hold));
  assert(i9100_peripheral_native_transport_close(&lease)==(fault?I9100_SDIO_CARD:I9100_SDIO_CRC));
  assert(lease.owned&&c->sdio.closing&&card->fault&&io.lock(io.cookie));
  /* Repair only the stand-in wire; never clear the production card's fault. */
  card_model::m.crc=card_model::m.r5_error=0;
  assert(!i9100_peripheral_native_transport_close(&lease)&&!lease.owned);restored(c);
 }
}
static void failed_restore_retains_resources()
{
 for(unsigned fault=0;fault<3;fault++){
  i9100_peripheral_native *c=fresh();i9100_native_transport_lease lease={};i9100_sdio *card=0;i9100_f2_io io;
  if(fault==0){
   card_model::m.crc=1;card_model::m.drop_feedback_restore=1;
   assert(open(c,&lease,&card,&io)==I9100_SDIO_CRC&&card&&lease.owned&&!io.cookie);
  }else{
   assert(!open(c,&lease,&card,&io));
   if(fault==1)card_model::m.drop_feedback_restore=1;else drop_sdio_release=1;
   assert(i9100_peripheral_native_transport_close(&lease)==(fault==1?I9100_SDIO_IO:I9100_CLOCK_IO));
  }
  assert(lease.owned&&card==&c->sdio.card&&c->sdio.live&&c->sdio.closing);
  assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
  unsigned commands=card_model::m.commands,init=wrapped_inits;
  i9100_native_transport_lease other={};i9100_sdio *other_card=0;i9100_f2_io other_io;
  assert(open(c,&other,&other_card,&other_io)==I9100_SDIO_BUSY&&!other.owned&&!other_card);
  assert(init==wrapped_inits&&commands==card_model::m.commands&&owned_sdio_lock(&lease));
  if(fault<2)assert(card->host_owned&&c->sdio.pins.active&&c->sdio.source_gate.active&&c->sdio.ip_gate.active);
  else assert(!card->host_owned&&!c->sdio.pins.active&&!c->sdio.source_gate.active&&c->sdio.ip_gate.active);
  card_model::m.crc=card_model::m.drop_feedback_restore=0;drop_sdio_release=0;
  assert(!i9100_peripheral_native_transport_close(&lease));restored(c);
 }
}
int main()
{
 enumeration_and_pio();holds_and_stale_callbacks();initialization_faults();
 transfer_fault_and_explicit_cleanup();failed_restore_retains_resources();
 puts("PASS real native SDIO open + real SDIO/CIS/F1/CMD52/53 driver, live mutex/time callbacks, F2-off, borrower/stale exclusion, bounded CRC/R5/CIS/clock/reset/ready faults and retained failed restore; SDHCI/card/IOKit are stand-ins, no phone/native-MMIO/electrical/FW/RF claim");
 return 0;
}
