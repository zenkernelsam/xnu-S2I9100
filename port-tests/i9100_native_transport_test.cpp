/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_native_parent_model.h"
static void uart_ownership()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease l={};i9100_bt_io io;
 unsigned before=barriers;
 assert(i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,0,&io)==I9100_UART_ARGUMENT&&barriers==before&&!l.owned);
 assert(!i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)&&l.owned);
 assert(l.kind==I9100_NATIVE_UART&&l.self==(uintptr_t)&l&&io.cookie==&l);
 i9100_native_transport_lease copy=l;before=barriers;
 assert(i9100_peripheral_native_transport_close(&copy)==I9100_GPIO_OWNER&&barriers==before);
 assert(i9100_peripheral_native_uart_close(c)==I9100_UART_BUSY&&barriers==before);
 uint8_t buffer[8];unsigned got=99;
 assert(i9100_peripheral_native_uart_read(c,buffer,sizeof(buffer),&got)==I9100_UART_BUSY&&!got&&barriers==before);
 assert(i9100_peripheral_native_uart_read_owned(&l,buffer,sizeof(buffer),&got)==0&&!got);
 assert(i9100_peripheral_native_uart_read_owned(&l,(uint8_t *)&l,8,&got)==I9100_UART_ARGUMENT);
 assert(i9100_peripheral_native_transport_idle(c,I9100_NATIVE_UART)==I9100_GPIO_BUSY);
 assert(!io.lock(io.cookie));before=barriers;
 assert(i9100_peripheral_native_transport_close(&l)==I9100_UART_BUSY&&barriers==before&&l.owned);io.unlock(io.cookie);
 uint32_t generation=l.generation;l.generation--;
 assert(io.lock(io.cookie)&&i9100_peripheral_native_transport_close(&l)==I9100_UART_BUSY);l.generation=generation;
 i9100_bt_io old=io;assert(!i9100_peripheral_native_transport_close(&l)&&!l.owned);
 before=barriers;assert(old.lock(old.cookie)&&barriers==before);
 assert(!i9100_peripheral_native_transport_idle(c,I9100_NATIVE_UART));
 i9100_native_transport_lease second={};assert(!i9100_peripheral_native_uart_open_owned(c,&second,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(second.generation>generation&&old.lock(old.cookie));
 assert(!i9100_peripheral_native_transport_close(&second));
 c->uart.generation=0xffffffffU;before=barriers;i9100_native_transport_lease exhausted={};
 assert(i9100_peripheral_native_uart_open_owned(c,&exhausted,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&barriers==before&&!exhausted.owned);
 assert(!i9100_peripheral_native_destroy(&c)&&!c&&!active_allocations);
}
static void protocol_holds()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease lease={};i9100_bt_io io;
 assert(!i9100_peripheral_native_uart_open_owned(c,&lease,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 i9100_native_transport_hold session={},worker={};unsigned before=barriers;
 assert(!i9100_peripheral_native_transport_hold_get(&lease,&session)&&!i9100_peripheral_native_transport_hold_get(&lease,&worker));
 assert(c->uart.borrowers==2U&&!i9100_peripheral_native_transport_hold_check(&session)&&barriers==before);
 assert(i9100_peripheral_native_transport_close(&lease)==I9100_UART_BUSY&&!c->uart.closing&&barriers==before);
 assert(!io.lock(io.cookie));assert(i9100_peripheral_native_transport_hold_put(&worker)==I9100_GPIO_BUSY&&worker.owned);io.unlock(io.cookie);
 i9100_native_transport_hold copy=worker;assert(i9100_peripheral_native_transport_hold_put(&copy)==I9100_GPIO_OWNER&&c->uart.borrowers==2U);
 worker.generation++;assert(i9100_peripheral_native_transport_hold_check(&worker)==I9100_GPIO_OWNER);worker.generation--;
 assert(!i9100_peripheral_native_transport_hold_put(&worker)&&c->uart.borrowers==1U);
 assert(i9100_peripheral_native_transport_hold_put(&worker)==I9100_GPIO_OWNER);
 c->uart.port.fault=1;assert(i9100_peripheral_native_transport_hold_check(&session)==I9100_GPIO_IO);
 assert(!i9100_peripheral_native_transport_hold_put(&session)&&!c->uart.borrowers);
 c->uart.port.fault=0; /* explicit model recovery only, not driver behavior */
 assert(!i9100_peripheral_native_transport_close(&lease));
 assert(i9100_peripheral_native_transport_hold_get(&lease,&worker)==I9100_GPIO_ARGUMENT);
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
 /* Validate SDIO ownership branches using cached fault injection; this does
  * not claim the native host has completed real F1 enumeration. */
 setup();c=parent();lease={};session={};worker={};lease.context=c;lease.self=(uintptr_t)&lease;
 lease.kind=I9100_NATIVE_SDIO;lease.generation=lease.owned=1;c->sdio.owner=&lease;c->sdio.live=c->sdio.generation=1;c->sdio.card.active=1;
 before=barriers;assert(!i9100_peripheral_native_transport_hold_get(&lease,&session)&&c->sdio.borrowers==1U&&barriers==before);
 assert(i9100_peripheral_native_transport_close(&lease)==I9100_SDIO_BUSY&&!c->sdio.closing&&barriers==before);
 c->sdio.card.fault=1;assert(i9100_peripheral_native_transport_hold_check(&session)==I9100_GPIO_IO);
 assert(!i9100_peripheral_native_transport_hold_put(&session));c->sdio.closing=1;
 assert(i9100_peripheral_native_transport_hold_get(&lease,&worker)==I9100_GPIO_BUSY&&!worker.owned);
 c->sdio.closing=0;c->sdio.card.fault=0;c->sdio.borrowers=0xffffffffU;
 assert(i9100_peripheral_native_transport_hold_get(&lease,&worker)==I9100_GPIO_BUSY);c->sdio.borrowers=0;
 c->sdio.live=c->sdio.card.active=0;c->sdio.owner=0;lease.owned=0;
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void retained_uart_close()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease l={};i9100_bt_io io;
 assert(!i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 memory[0x13800000U][16/4]=0;
 assert(i9100_peripheral_native_transport_close(&l)==I9100_UART_TIMEOUT&&l.owned&&c->uart.closing);
 unsigned before=barriers;assert(io.lock(io.cookie)&&barriers==before);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 memory[0x13800000U][16/4]=4;
 assert(!i9100_peripheral_native_transport_close(&l)&&!l.owned);assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void legacy_compatibility()
{
 setup();i9100_peripheral_native *c=parent();i9100_bt_io io;
 assert(!i9100_peripheral_native_uart_open(c,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(!c->uart.owner&&!io.lock(io.cookie));unsigned sent=0;const uint8_t command[4]={1,3,12,0};
 assert(!io.write(io.cookie,command,sizeof(command),&sent)&&sent==sizeof(command));io.unlock(io.cookie);
 uint8_t bytes[8];unsigned received=99;
 assert(!i9100_peripheral_native_uart_read(c,bytes,sizeof(bytes),&received)&&!received);
 i9100_bt_io old=io;assert(!i9100_peripheral_native_uart_close(c));
 i9100_native_transport_lease lease={};
 assert(!i9100_peripheral_native_uart_open_owned(c,&lease,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(old.lock(old.cookie)); /* a former legacy borrower cannot access the new owner */
 assert(!i9100_peripheral_native_transport_close(&lease));
 i9100_sdio *card=0;i9100_f2_io f2;
 assert(i9100_peripheral_native_sdio_open(c,48000000U,1,&card,&f2)==I9100_SDIO_CARD&&!card&&!c->sdio.owner&&!c->sdio.live);
 assert(!i9100_peripheral_native_transport_idle(c,I9100_NATIVE_SDIO));
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void sdio_failure_ownership()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease l={};i9100_f2_io io;i9100_sdio *card=0;
 /* Version/capability absent: actual native open enters then safely unwinds,
  * no F1 chip assumed. Force only IP gate restore loss to retain the owner. */
 drop_sdio_release=1;
 assert(i9100_peripheral_native_sdio_open_owned(c,&l,48000000U,1,&card,&io)==I9100_SDIO_CARD);
 assert(l.owned&&card&&c->sdio.closing&&c->sdio.owner==&l);
 unsigned before=barriers;
 assert(i9100_peripheral_native_sdio_close(c)==I9100_SDIO_BUSY&&barriers==before);
 i9100_native_transport_lease other={};i9100_sdio *other_card=0;
 assert(i9100_peripheral_native_sdio_open_owned(c,&other,48000000U,1,&other_card,&io)==I9100_SDIO_BUSY&&!other.owned&&barriers==before);
 assert(owned_sdio_lock(&l)&&barriers==before);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 drop_sdio_release=0;
 assert(!i9100_peripheral_native_transport_close(&l)&&!l.owned);
 assert(!i9100_peripheral_native_transport_idle(c,I9100_NATIVE_SDIO));
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void allocations_and_aliases()
{
 setup();i9100_peripheral_native *c=0;assert(i9100_peripheral_native_create(0,&c)==I9100_GPIO_DISABLED&&!allocations&&!barriers);
 c=parent();i9100_native_transport_lease l={};i9100_bt_io io;unsigned before=barriers;
 assert(i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,1,(i9100_bt_io *)&l)==I9100_UART_ARGUMENT&&barriers==before);
 assert(i9100_peripheral_native_uart_open_owned(c,(i9100_native_transport_lease *)c,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_ARGUMENT&&barriers==before);
 assert(!i9100_peripheral_native_destroy(&c));unsigned count=allocations;
 for(unsigned n=1;n<=count;n++){
  setup();fail_alloc=n;
  assert(i9100_peripheral_native_create(1,&c)&&!c&&!active_allocations);
 }
 setup();c=parent();i9100_native_transport_lease fresh={};interrupts=false;before=barriers;
 assert(i9100_peripheral_native_uart_open_owned(c,&fresh,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&!fresh.owned&&barriers==before);
 interrupts=true;assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void board_reservation()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease uart={},sdio={},other={};i9100_bt_io io;
 unsigned before=barriers;
 assert(!i9100_peripheral_native_radio_bind(c,&uart,&sdio)&&barriers==before);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c&&barriers==before);
 assert(i9100_peripheral_native_uart_open(c,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&barriers==before);
 assert(i9100_peripheral_native_uart_open_owned(c,&other,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&!other.owned&&barriers==before);
 assert(i9100_peripheral_native_radio_bind(c,&uart,&sdio)==I9100_GPIO_BUSY&&barriers==before);
 assert(!i9100_peripheral_native_uart_open_owned(c,&uart,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(i9100_peripheral_native_radio_unbind(c,&uart,&sdio)==I9100_GPIO_BUSY);
 assert(!i9100_peripheral_native_transport_close(&uart));
 assert(i9100_peripheral_native_radio_unbind(c,&other,&sdio)==I9100_GPIO_OWNER);
 assert(!i9100_peripheral_native_radio_unbind(c,&uart,&sdio));
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
int main()
{uart_ownership();protocol_holds();retained_uart_close();legacy_compatibility();sdio_failure_ownership();allocations_and_aliases();board_reservation();puts("PASS actual native UART/SDIO reservations/session+worker holds/no close while borrowed/copy-stale hold/put after fault/owner+epoch/legacy exclusion/close-in-flight/failure retention/all allocations; MMIO stand-in and cached SDIO hold injection only, no successful native F1 enumeration claim");return 0;}
