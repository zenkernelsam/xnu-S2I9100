/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_radio_native.h"
#include "i9100_native_parent_model.h"
struct supply_model {unsigned held,get_calls,put_calls,verify_calls,partial_get,put_fail,verify_fail_at;};
static supply_model supply;
static uint32_t gpio_reg(unsigned off){return memory[0x11000000U][off/4U];}
static void off_pins(){assert(!(gpio_reg(0xc4U)&16U)&&!(gpio_reg(0xe4U)&5U)&&!(gpio_reg(0xc64U)&2U));}
static int get_supply(void *cookie,unsigned *owned)
{
 supply_model *s=(supply_model *)cookie;off_pins();
 assert(((gpio_reg(0xc0U)>>16)&15U)==1U&&((gpio_reg(0xe0U)>>8)&15U)==1U&&(gpio_reg(0xe0U)&15U)==1U&&((gpio_reg(0xc60U)>>4)&15U)==1U);
 s->held=1;s->get_calls++;*owned=1;return s->partial_get?-1:0;
}
static int verify_supply(void *cookie)
{supply_model *s=(supply_model *)cookie;assert(s->held);return ++s->verify_calls==s->verify_fail_at?-1:0;}
static int put_supply(void *cookie)
{supply_model *s=(supply_model *)cookie;off_pins();s->put_calls++;if(s->put_fail)return -1;s->held=0;return 0;}
static i9100_radio_supply supplied={&supply,get_supply,verify_supply,put_supply};
static i9100_peripheral_native *setup_radio_parent(){setup();memset(&supply,0,sizeof(supply));return parent();}
static i9100_radio_native *radio(i9100_peripheral_native *c)
{i9100_radio_native *r=0;assert(!i9100_radio_native_open(1,c,&supplied,1,&r)&&r);return r;}
static i9100_radio_status status(i9100_radio_native *r)
{i9100_radio_status s;assert(!i9100_radio_native_status(r,&s));return s;}
static void close(i9100_peripheral_native *&c,i9100_radio_native *&r)
{assert(!i9100_radio_native_close(&r)&&!r&&!supply.held);off_pins();assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);}
static void normal()
{
 i9100_peripheral_native *c=setup_radio_parent();unsigned before=allocations;i9100_radio_native *r=0;
 assert(i9100_radio_native_open(0,c,&supplied,1,&r)==I9100_RADIO_DISABLED&&!r&&allocations==before);
 r=radio(c);i9100_radio_status s=status(r);assert(s.ready&&s.native_bound&&!s.powered[0]&&!s.powered[1]);
 i9100_radio_native *old=r;before=allocations;
 assert(i9100_radio_native_open(1,c,&supplied,1,&r)==I9100_RADIO_BUSY&&r==old&&allocations==before);
 i9100_bt_io io;before=barriers;
 assert(i9100_radio_native_uart_open(r,48000000U,115200U,I9100_UART_RAW_FRACTION,&io)==I9100_RADIO_DISABLED&&barriers==before);
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_BT,1)&&ns==70000000ULL);
 assert(!i9100_radio_native_uart_open(r,48000000U,115200U,I9100_UART_RAW_FRACTION,&io));
 i9100_native_transport_hold borrowed={};assert(!i9100_radio_native_borrow(r,I9100_RADIO_BT,&borrowed));before=barriers;
 assert(i9100_radio_native_transport_close(r,I9100_RADIO_BT)==I9100_UART_BUSY&&!c->uart.closing&&barriers==before);
 assert(!i9100_peripheral_native_transport_hold_put(&borrowed));
 s=status(r);assert(s.native_uart_owned&&s.holds[1]==1U&&s.generation[1]==1U);
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_WLAN,1));
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_WLAN,0));
 assert(status(r).native_uart_owned&&status(r).holds[1]==1U&&status(r).powered[1]);
 before=barriers;assert(i9100_radio_native_set_power(r,I9100_RADIO_BT,0)==I9100_RADIO_BUSY&&barriers==before);
 assert(i9100_peripheral_native_uart_close(c)==I9100_UART_BUSY);
 assert(i9100_radio_native_close(&r)==I9100_RADIO_BUSY&&r&&status(r).native_closing);
 assert(!io.lock(io.cookie));const uint8_t cmd[4]={1,3,12,0};unsigned sent=0;
 assert(!io.write(io.cookie,cmd,4,&sent)&&sent==4U);io.unlock(io.cookie);
 assert(!i9100_radio_native_transport_close(r,I9100_RADIO_BT));close(c,r);
 c=setup_radio_parent();r=radio(c);
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_WLAN,1));assert(!i9100_radio_native_set_power(r,I9100_RADIO_BT,1));
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_WLAN,0));assert(gpio_reg(0xc4U)&16U);assert(gpio_reg(0xe4U)&1U);
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_WLAN,1)&&status(r).generation[0]==2U);
 i9100_sdio *card=0;i9100_f2_io f2;
 assert(i9100_radio_native_sdio_open(r,48000000U,&card,&f2)==I9100_SDIO_CARD&&!card);
 s=status(r);assert(!s.native_sdio_owned&&!s.holds[0]&&s.powered[0]);close(c,r);
}
static void retained_errors()
{
 i9100_peripheral_native *c=setup_radio_parent();i9100_radio_native *r=radio(c);i9100_bt_io io;
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_BT,1));assert(!i9100_radio_native_uart_open(r,48000000U,115200U,I9100_UART_RAW_FRACTION,&io));
 memory[0x13800000U][16/4U]=0;
 assert(i9100_radio_native_transport_close(r,I9100_RADIO_BT)==I9100_UART_TIMEOUT);
 i9100_radio_status s=status(r);assert(s.holds[1]==1U&&s.native_uart_owned);
 assert(i9100_radio_native_set_power(r,I9100_RADIO_BT,0)==I9100_RADIO_BUSY);
 assert(i9100_radio_native_close(&r)==I9100_RADIO_BUSY&&r);assert(io.lock(io.cookie));
 memory[0x13800000U][16/4U]=4U;assert(!i9100_radio_native_transport_close(r,I9100_RADIO_BT));close(c,r);
 c=setup_radio_parent();r=radio(c);assert(!i9100_radio_native_set_power(r,I9100_RADIO_WLAN,1));
 drop_sdio_release=1;i9100_sdio *card=0;i9100_f2_io f2;
 assert(i9100_radio_native_sdio_open(r,48000000U,&card,&f2)==I9100_SDIO_CARD&&card);
 s=status(r);assert(s.holds[0]==1U&&s.native_sdio_owned);
 assert(i9100_radio_native_set_power(r,I9100_RADIO_WLAN,0)==I9100_RADIO_BUSY);
 assert(i9100_radio_native_close(&r)==I9100_RADIO_BUSY&&r);drop_sdio_release=0;
 assert(!i9100_radio_native_transport_close(r,I9100_RADIO_WLAN));close(c,r);
 c=setup_radio_parent();supply.partial_get=supply.put_fail=1;r=0;
 assert(i9100_radio_native_open(1,c,&supplied,1,&r)==I9100_RADIO_IO&&r);
 s=status(r);assert(s.supply_owned&&s.native_bound&&s.pins_owned==3U);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);supply.put_fail=0;close(c,r);
 c=setup_radio_parent();r=radio(c);supply.put_fail=1;
 assert(i9100_radio_native_close(&r)==I9100_RADIO_IO&&r&&status(r).supply_owned);supply.put_fail=0;close(c,r);
}
static void preflight_and_timing()
{
 i9100_peripheral_native *c=setup_radio_parent();i9100_radio_native *r=0;memory[0x11000000U][0xe4U/4U]=4U;
 assert(i9100_radio_native_open(1,c,&supplied,1,&r)==I9100_RADIO_NOT_READY&&!r&&!supply.get_calls);
 assert(gpio_reg(0xe4U)&4U);assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
 c=setup_radio_parent();supply.verify_fail_at=1;
 assert(i9100_radio_native_open(1,c,&supplied,1,&r)==I9100_RADIO_NOT_READY&&!r&&!supply.held);assert(!i9100_peripheral_native_destroy(&c));
 c=setup_radio_parent();r=radio(c);freeze_model_time=true;
 assert(i9100_radio_native_set_power(r,I9100_RADIO_BT,1)==I9100_RADIO_TIMEOUT&&!status(r).powered[1]&&!status(r).generation[1]);freeze_model_time=false;close(c,r);
 c=setup_radio_parent();i9100_bt_io io;assert(!i9100_peripheral_native_uart_open(c,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(i9100_radio_native_open(1,c,&supplied,1,&r)==I9100_GPIO_BUSY&&!r&&!supply.get_calls&&c->uart.live);
 assert(!i9100_peripheral_native_uart_close(c));assert(!i9100_peripheral_native_destroy(&c));
 for(unsigned n=1;n<=2U;n++){
  c=setup_radio_parent();unsigned baseline=active_allocations;fail_alloc=allocations+n;
  assert(i9100_radio_native_open(1,c,&supplied,1,&r)==I9100_RADIO_BUSY&&!r&&active_allocations==baseline);
  assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
 }
}
static void live_supply_checks()
{
 i9100_peripheral_native *c=setup_radio_parent();i9100_radio_native *r=radio(c);i9100_bt_io io;
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_BT,1));supply.verify_fail_at=supply.verify_calls+1U;
 unsigned before=barriers;
 assert(i9100_radio_native_uart_open(r,48000000U,115200U,I9100_UART_RAW_FRACTION,&io)==I9100_RADIO_NOT_READY&&barriers==before);
 assert(!status(r).native_uart_owned&&!status(r).holds[1]);close(c,r);
 c=setup_radio_parent();r=radio(c);assert(!i9100_radio_native_set_power(r,I9100_RADIO_WLAN,1));
 memory[0x11000000U][0xe4U/4U]&=~4U;i9100_sdio *card=0;i9100_f2_io f2;
 assert(i9100_radio_native_sdio_open(r,48000000U,&card,&f2)==I9100_RADIO_IO&&!status(r).native_sdio_owned&&!status(r).holds[0]);close(c,r);
}
int main()
{normal();retained_errors();preflight_and_timing();live_supply_checks();puts("PASS real GPIO/native host reservation+live-supply/pin proof before open/holds/ROM20+50ms/independent roles/cold generations/active-host exclusion/failed UART+SDIO close/partial supply/pin lifetime; host models only, no real32k or native F1/FW boot");return 0;}
