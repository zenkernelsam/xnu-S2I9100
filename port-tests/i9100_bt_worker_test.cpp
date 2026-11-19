/* SPDX-License-Identifier: BSD-2-Clause; true native UART/power and boot worker. */
#define main radio_native_reference_main
#include "i9100_radio_native_test.cpp"
#undef main
#include "i9100_bt_worker.h"
#include "offline-native-sdk/IOKit/i9100_model_events.h"
#include <openssl/sha.h>
static IOWorkLoop *bt_loop;
static unsigned event_add,fail_event_add,fail_event_remove,fail_event_timer,rx_delivered,reenter_close;
static i9100_bt_worker *worker;
static struct {
 bool enabled,drop_ack,tx_stuck,rx_error,reply_error;unsigned commands,assembled,reset_count;
 uint8_t command[259];std::vector<uint8_t> fifo;unsigned head;
 i9100_uart_io original;i9100_bt_boot_profile profile;
} peer;
void *i9100_model_event_allocate(size_t n){return allocate()?calloc(1,n):nullptr;}
void i9100_model_event_free(void *p){assert(p&&active_allocations);active_allocations--;free(p);}
IOWorkLoop *IOWorkLoop::workLoop(){bt_loop=new IOWorkLoop;return bt_loop;}
IOReturn IOWorkLoop::addEventSource(IOEventSource *s)
{if(++event_add==fail_event_add)return -99;assert(count<4);s->retain();s->setWorkLoop(this);sources[count++]=s;return 0;}
IOReturn IOWorkLoop::removeEventSource(IOEventSource *s)
{if(fail_event_remove)return -98;unsigned n=0;while(n<count&&sources[n]!=s)n++;assert(n<count);s->setWorkLoop(nullptr);s->release();for(unsigned k=n;k+1<count;k++)sources[k]=sources[k+1];sources[--count]=nullptr;return 0;}
IOReturn IOWorkLoop::runAction(Action f,OSObject *o,void *a,void *b,void *c,void *d)
{assert(interrupts);bool old=gate;gate=true;int result=f(o,a,b,c,d);gate=old;return result;}
void IOEventSource::signalWorkAvailable(){assert(workLoop);workLoop->pending=true;}
void IOWorkLoop::pump(){assert(!gate);thread=gate=true;for(unsigned n=0;n<count;n++)sources[n]->checkForWork();thread=gate=false;}
void IOWorkLoop::tick(){assert(!gate);thread=gate=true;for(unsigned n=0;n<count;n++)if(auto *t=dynamic_cast<IOTimerEventSource *>(sources[n]))t->fire();thread=gate=false;}
void IOWorkLoop::free(){assert(!count&&!gate);}
IOReturn IOTimerEventSource::setTimeoutUS(UInt32 us){assert(us>=500U&&us<=10000U);if(fail_event_timer)return -97;queued=true;return 0;}
static uint32_t uart_read(void *cookie,unsigned off)
{
 auto *u=(native_uart *)cookie;assert(u->controller->held);if(off==24U)return peer.fifo.size()-peer.head>255U?256U:(uint32_t)(peer.fifo.size()-peer.head)|(peer.tx_stuck?(1U<<24):0U);
 if(off==20U)return peer.rx_error?4U:0U;return memory[0x13800000U][off/4U];
}
static void uart_write(void *cookie,unsigned off,uint32_t value)
{auto *u=(native_uart *)cookie;assert(u->controller->held);memory[0x13800000U][off/4U]=value;}
static uint8_t uart_rx(void *cookie,unsigned off)
{auto *u=(native_uart *)cookie;assert(u->controller->held&&off==36U&&peer.head<peer.fifo.size());return peer.fifo[peer.head++];}
static void enqueue_reply(uint16_t op)
{
 if(peer.drop_ack)return;uint8_t result[32]={4,0x0e,4,1,(uint8_t)op,(uint8_t)(op>>8),(uint8_t)(peer.reply_error?1U:0U)};unsigned length=7;
 if(op==0x0c03U)peer.reset_count++;
 else if(op==0x1001U){auto v=peer.reset_count<=1U?peer.profile.rom:peer.profile.patched;
  result[2]=12;result[7]=v.hci_version;result[8]=(uint8_t)v.hci_revision;result[9]=(uint8_t)(v.hci_revision>>8);result[10]=v.lmp_version;
  result[11]=(uint8_t)v.manufacturer;result[12]=(uint8_t)(v.manufacturer>>8);result[13]=(uint8_t)v.lmp_subversion;result[14]=(uint8_t)(v.lmp_subversion>>8);length=15;
 }else if(op==0x1009U){result[2]=10;memcpy(result+7,peer.profile.factory_bdaddr_hci,6);length=13;
 }else if(op==0x1005U){result[2]=11;result[7]=0xfd;result[8]=3;result[9]=64;result[10]=8;length=14;
 }else if(op==0x1003U){result[2]=12;result[7]=0xff;length=15;}
 peer.fifo.insert(peer.fifo.end(),result,result+length);
}
static void uart_tx(void *cookie,unsigned off,uint8_t value)
{
 auto *u=(native_uart *)cookie;assert(u->controller->held&&off==32U&&peer.assembled<sizeof(peer.command));peer.command[peer.assembled++]=value;
 if(peer.assembled>=4U&&peer.assembled==4U+peer.command[3]){
  assert(peer.command[0]==1);uint16_t op=peer.command[1]|((uint16_t)peer.command[2]<<8);peer.commands++;enqueue_reply(op);peer.assembled=0;
 }
}
extern "C" int __real_i9100_uart_bt_init(i9100_uart_bluetooth *,const i9100_uart_io *);
extern "C" int __wrap_i9100_uart_bt_init(i9100_uart_bluetooth *u,const i9100_uart_io *io)
{
 if(!peer.enabled)return __real_i9100_uart_bt_init(u,io);peer.original=*io;auto model=*io;
 model.read32=uart_read;model.read8=uart_rx;model.write32=uart_write;model.write8=uart_tx;return __real_i9100_uart_bt_init(u,&model);
}
static int sha(void *,const void *bytes,uint32_t n,uint8_t result[32]){assert(SHA256((const uint8_t *)bytes,n,result));return 0;}
static int rx(void *,const i9100_bt_frame *frame)
{assert(bt_loop->thread&&!peer.original.lock(peer.original.cookie));peer.original.unlock(peer.original.cookie);assert(frame->bytes&&frame->length);rx_delivered++;
 if(reenter_close)assert(i9100_bt_worker_destroy(&worker)==I9100_BT_BOOT_BUSY&&worker);return 0;}
static uint8_t firmware[]={0x4c,0xfc,5,0,0,0x20,0,0xab,0x4e,0xfc,4,1,0,0x20,0};
static i9100_bt_worker_profile wp()
{
 i9100_bt_worker_profile p={};p.verified_uart_clock_hz=48000000;p.poll_us=1000;p.fraction=I9100_UART_RAW_FRACTION;
 p.controller.board_data_reviewed=1;p.controller.ram_base=0x00200000;p.controller.ram_bytes=0x10000;p.controller.launch_address=0x00200001;p.controller.timeout_ms=30000;
 p.controller.rom={6,6,1,15,0x4103};p.controller.patched={6,6,2,15,0x4103};uint8_t bd[6]={1,2,3,4,5,6};memcpy(p.controller.factory_bdaddr_hci,bd,6);
 assert(!sha(nullptr,firmware,sizeof(firmware),p.controller.hcd_sha256));return p;
}
static i9100_bcm_fw_integrity integrity={nullptr,sha};static i9100_bt_output output={nullptr,rx};
static struct i9100_bt_worker_status worker_status()
{struct i9100_bt_worker_status s={};assert(!i9100_bt_worker_status(worker,&s));return s;}
static void start_context(i9100_peripheral_native **parent,i9100_radio_native **r)
{
 *parent=setup_radio_parent();*r=radio(*parent);assert(!i9100_radio_native_set_power(*r,I9100_RADIO_BT,1));
 peer={};peer.enabled=true;peer.profile=wp().controller;bt_loop=nullptr;event_add=fail_event_add=fail_event_remove=fail_event_timer=rx_delivered=reenter_close=0;
}
static void finish_worker(i9100_peripheral_native *&parent,i9100_radio_native *&r)
{assert(!i9100_bt_worker_destroy(&worker)&&!worker);peer.enabled=false;close(parent,r);}
int main()
{
 assert(!radio_native_reference_main());i9100_peripheral_native *c=nullptr;i9100_radio_native *r=nullptr;
 start_context(&c,&r);auto p=wp();unsigned before=allocations;
 assert(i9100_bt_worker_create(0,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker)==I9100_BT_BOOT_DISABLED&&!worker&&allocations==before);
 assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 assert(!peer.commands&&worker_status().session_hold&&worker_status().worker_hold&&c->uart.borrowers==2U);
 assert(i9100_radio_native_transport_close(r,I9100_RADIO_BT)==I9100_UART_BUSY&&!c->uart.closing);
 assert(!i9100_bt_worker_start(worker));
 for(unsigned n=0;n<700U&&!worker_status().ready;n++){ns+=1000000;bt_loop->tick();}
 assert(worker_status().ready&&!worker_status().fault&&worker_status().boot.confirmed_records==2U&&peer.commands==11U);
 reenter_close=1;uint8_t event[]={4,0xff,1,0xab};peer.fifo.insert(peer.fifo.end(),event,event+4);ns++;bt_loop->tick();assert(rx_delivered==1);
 assert(!i9100_bt_worker_command(worker,0x1001U,nullptr,0));ns++;bt_loop->tick();assert(rx_delivered==2);
 assert(!i9100_bt_worker_destroy(&worker)&&!worker);before=peer.commands;
 assert(i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker)==I9100_BT_BOOT_OWNER&&!worker&&peer.commands==before);
 peer.enabled=false;close(c,r);
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 assert(!i9100_bt_worker_destroy(&worker)&&!worker);
 assert(!i9100_radio_native_set_power(r,I9100_RADIO_BT,0)&&!i9100_radio_native_set_power(r,I9100_RADIO_BT,1));
 peer.fifo.clear();peer.head=peer.assembled=peer.reset_count=0;
 assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker)&&worker_status().boot.generation==2U);
 finish_worker(c,r);
 start_context(&c,&r);p=wp();p.controller.hcd_sha256[0]^=1;before=peer.commands;
 assert(i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker)==I9100_BT_BOOT_HASH&&!worker&&!status(r).native_uart_owned&&peer.commands==before);
 peer.enabled=false;close(c,r);
 for(unsigned point=1;point<=5;point++){
  start_context(&c,&r);p=wp();fail_alloc=allocations+point;
  int e=i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker);
  assert(e&&!worker);fail_alloc=0;peer.enabled=false;close(c,r);
 }
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 fail_event_timer=1;assert(i9100_bt_worker_start(worker)==I9100_BT_BOOT_IO&&worker_status().fault&&!worker_status().running);
 fail_event_timer=0;finish_worker(c,r);
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));assert(!i9100_bt_worker_start(worker));
 supply.verify_fail_at=supply.verify_calls+1U;ns++;bt_loop->tick();assert(worker_status().fault&&worker_status().boot.error==I9100_BT_BOOT_OWNER);
 supply.verify_fail_at=0;finish_worker(c,r);
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));assert(!i9100_bt_worker_start(worker));
 memory[0x13800000U][16/4]=0;assert(i9100_bt_worker_destroy(&worker)!=0&&worker&&worker_status().session_hold&&status(r).native_uart_owned);
 memory[0x13800000U][16/4]=4;finish_worker(c,r);
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 fail_event_remove=1;assert(i9100_bt_worker_destroy(&worker)==-98&&worker&&worker_status().session_hold);fail_event_remove=0;finish_worker(c,r);
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 memory[0x11000000U][0xc4U/4U]&=~16U;
 assert(i9100_bt_worker_destroy(&worker)!=0&&worker&&worker_status().session_hold&&status(r).native_uart_owned);
 memory[0x11000000U][0xc4U/4U]|=16U;finish_worker(c,r);
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 assert(!i9100_bt_worker_start(worker));assert(i9100_radio_native_close(&r)==I9100_RADIO_BUSY&&r);
 unsigned closed_commands=peer.commands;ns++;bt_loop->tick();assert(worker_status().fault&&peer.commands==closed_commands);
 finish_worker(c,r); /* parent closing cannot deadlock the child's qualified UART cleanup */
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 assert(!i9100_bt_worker_start(worker));peer.drop_ack=true;peer.fifo.clear();peer.head=0;unsigned calls=peer.commands;
 ns+=1000000001ULL;bt_loop->tick();assert(worker_status().fault&&worker_status().error==I9100_BT_TIMEOUT&&peer.commands==calls);
 finish_worker(c,r);
 start_context(&c,&r);p=wp();assert(!i9100_bt_worker_create(1,r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 assert(!i9100_bt_worker_start(worker));peer.rx_error=true;ns++;bt_loop->tick();assert(worker_status().fault&&!worker_status().running);peer.rx_error=false;
 finish_worker(c,r);
 puts("PASS actual radio supply/GPIO -> native UART/register peer -> H4/BCM boot -> OSObject/workloop: fresh generation/two holds/private HCD/11 commands/ready/RX/command/reentry/fault cleanup retention/default OFF; hardware/IOKit/ROM stand-ins");return 0;
}
