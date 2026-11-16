/* SPDX-License-Identifier: BSD-2-Clause; synthetic MAC/SSID/image, real upper drivers. */
#include "i9100_bcm_runtime_model.h"
#include "i9100_bcm_pump.h"
static struct i9100_wlan_control control;
static struct i9100_bcm_pump pump;
static unsigned control_locked,pump_locked,delivered,callback_busy,drop_delivery,stop_in_callback;
static uint8_t sequence,seen[8];
static const uint8_t ap[6]={2,1,2,3,4,5};
static int take_lock(void *cookie){unsigned *v=cookie;if(*v)return -1;*v=1;return 0;}
static void drop_lock(void *cookie){unsigned *v=cookie;assert(*v);*v=0;}
static uint64_t control_now(void *cookie){(void)cookie;return now(&m);}
static int deliver(void *cookie,const uint8_t *p,unsigned bytes,unsigned priority)
{
 assert(cookie==&pump_locked&&pump_locked&&!control_locked&&!m.locked&&bytes==64U&&priority==3U);
 if(stop_in_callback){assert(i9100_bcm_pump_close(&pump)==I9100_PUMP_BUSY);callback_busy++;}
 if(drop_delivery)return 1;assert(delivered<8U);seen[delivered++]=p[14];return 0;
}
static void start(void)
{
 setup_boot();assert(!i9100_bcm_boot_start(&boot));memset(&control,0,sizeof(control));memset(&pump,0,sizeof(pump));
 control_locked=pump_locked=delivered=callback_busy=drop_delivery=stop_in_callback=0;sequence=0;
 struct i9100_wlan_io ci={&control_locked,take_lock,drop_lock,control_now};
 assert(!i9100_wlan_control_init(&control,1,&bus,&ci,boot.generation));
 struct i9100_pump_io pi={&pump_locked,take_lock,drop_lock,deliver};unsigned before=m.ops;
 assert(i9100_bcm_pump_init(&pump,0,&boot,&control,&pi)==I9100_PUMP_DISABLED&&!pump.initialized&&m.ops==before);
 assert(!i9100_bcm_pump_init(&pump,1,&boot,&control,&pi)&&pump.ready&&pump.bound==3U&&m.ops==before);
 assert(bus.rx_owner==&pump&&packets.rx_owner==&pump&&control.consumer==&pump);
 assert(i9100_wlan_control_close(&control)==I9100_WLAN_BUSY&&i9100_bcm_boot_stop(&boot)==I9100_BCM_BOOT_BUSY);
 assert(i9100_bcm_f2_enable(&packets,0,0)==I9100_F2_BUSY&&packets.enabled);
}
static void finish_pump(void)
{
 if(control.state!=I9100_WLAN_IDLE&&control.state!=I9100_WLAN_FAULT)assert(i9100_bcm_pump_abort(&pump)==I9100_PUMP_CANCELLED);
 assert(!i9100_bcm_pump_close(&pump)&&!control.consumer&&!bus.rx_owner&&!packets.rx_owner);
 assert(!i9100_wlan_control_close(&control));close_boot();
}
static void net16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void net32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4U;n++)p[n]=(uint8_t)(v>>(24U-8U*n));}
static uint8_t *queue(unsigned logical,unsigned channel)
{
 unsigned tail=logical>32U?logical-32U:0U;tail=tail>512U?(tail+511U)&~511U:(tail+3U)&~3U;
 unsigned wire=32U+tail;assert(rx_bytes+wire<=sizeof(rx));uint8_t *p=rx+rx_bytes;memset(p,0,wire);rx_bytes+=wire;
 put16(p,(uint16_t)logical);put16(p+2,(uint16_t)~logical);p[4]=sequence++;p[5]=(uint8_t)channel;p[7]=12;p[9]=(uint8_t)(bus.tx_sequence+32U);
 m.sdio_regs[0x20U/4U]|=0x40U;return p;
}
static void end_queue(void){assert(rx_bytes+32U<=sizeof(rx));memset(rx+rx_bytes,0,32);rx_bytes+=32;}
static void reset_queue(void){assert(rx_bytes==rx_cursor);rx_bytes=rx_cursor=0;}
static void ack(void)
{
 reset_queue();uint8_t mask[16]={0};const uint8_t *body=0;unsigned bytes=0;
 if(control.request_command==262U){body=mask;bytes=16;}
 else if(control.request_command==23U){body=ap;bytes=6;}
 uint8_t ssid[36]={0};if(control.request_command==25U){put32(ssid,6);memcpy(ssid+4,"S2-lab",6);body=ssid;bytes=36;}
 uint8_t *p=queue(28U+bytes,0);put32(p+12,control.request_command);put32(p+16,bytes);put32(p+20,(uint32_t)control.request_id<<16);
 if(bytes)memcpy(p+28,body,bytes);end_queue();assert(!i9100_bcm_pump_tick(&pump,8)&&!pump.pending_rx&&rx_bytes==rx_cursor);
}
static void event(unsigned type,unsigned flags)
{
 uint8_t *p=queue(88,1);p[12]=0x20;uint8_t *v=p+16;
 net16(v+12,0x886c);net16(v+14,0x8001);net16(v+16,58);v[20]=0x10;v[21]=0x18;net16(v+22,1);
 net16(v+24,2);net16(v+26,flags);net32(v+28,type);memcpy(v+48,ap,6);memcpy(v+54,"wlan0",5);
}
static void data(unsigned tag)
{uint8_t *p=queue(80,2);p[12]=0x20;p[13]=3;memcpy(p+16,ap,6);p[30]=(uint8_t)tag;}
static void linked(void)
{
 start();struct i9100_wlan_config config={0};config.ssid_bytes=6;memcpy(config.ssid,"S2-lab",6);memcpy(config.bssid,ap,6);
 assert(!i9100_wlan_join(&control,&config));assert(!i9100_bcm_pump_tick(&pump,8)&&control.pending);
 for(unsigned n=0;n<12U&&control.state==I9100_WLAN_CONFIGURING;n++){
  if(control.pending)ack();else assert(!i9100_bcm_pump_tick(&pump,8));
 }
 assert(control.state==I9100_WLAN_JOINING&&!control.pending);reset_queue();event(0,0);event(16,1);end_queue();
 assert(!i9100_bcm_pump_tick(&pump,8)&&control.pending&&control.request_command==23U);ack();assert(control.request_command==25U&&control.pending);ack();
 assert(control.state==I9100_WLAN_UP&&pump.event_frames==2U&&pump.control_frames>=10U);
}
static void idle_and_ownership(void)
{
 start();unsigned before=rx_wire_calls;assert(!i9100_bcm_pump_tick(&pump,8)&&rx_wire_calls==before&&!pump.pending_rx);
 uint8_t output[2048]={0};unsigned bytes=99;struct i9100_bcm_frame frame;
 assert(i9100_bcm_f2_receive(&packets,output,sizeof(output),&bytes)==I9100_F2_BUSY&&!bytes&&rx_wire_calls==before);
 assert(i9100_bcm_receive(&bus,output,12,&frame)==I9100_BCM_BUSY);
 struct i9100_bcm_pump copy=pump;assert(i9100_bcm_pump_tick(&copy,8)==I9100_PUMP_ARGUMENT);
 m.locked=1;assert(i9100_bcm_pump_tick(&pump,8)==I9100_PUMP_BUSY&&!pump.fault);m.locked=0;
 assert(i9100_bcm_pump_tick(&pump,0)==I9100_PUMP_ARGUMENT);finish_pump();
}
static void budget_and_data(void)
{
 linked();reset_queue();data(1);data(2);data(3);end_queue();stop_in_callback=1;
 assert(!i9100_bcm_pump_tick(&pump,2)&&pump.pending_rx&&delivered==2U&&seen[0]==1&&seen[1]==2);
 assert(!(m.sdio_regs[0x20U/4U]&0x40U)&&pump.budget_yields==1U);
 assert(!i9100_bcm_pump_tick(&pump,2)&&!pump.pending_rx&&delivered==3U&&seen[2]==3&&callback_busy==3U);
 for(unsigned n=0;n<sizeof(pump.frame);n++)assert(!pump.frame[n]);
 unsigned before=rx_wire_calls;assert(!i9100_bcm_pump_tick(&pump,8)&&rx_wire_calls==before);
 assert(i9100_bcm_pump_close(&pump)==I9100_PUMP_BUSY&&pump.ready&&!pump.closing);
 reset_queue();data(4);end_queue();drop_delivery=1;assert(!i9100_bcm_pump_tick(&pump,8)&&pump.dropped_frames==1U&&delivered==3U);
 finish_pump();
}
static void mailbox_flow_and_faults(void)
{
 linked();uint8_t ethernet[64]={0};m.sdio_regs[0x20U/4U]=0x30U;assert(!i9100_bcm_pump_tick(&pump,8)&&bus.data_paused);
 assert(i9100_wlan_send(&control,ethernet,64,0)==I9100_BCM_CREDIT);
 m.sdio_regs[0x20U/4U]=0;assert(!i9100_bcm_pump_tick(&pump,8)&&!bus.data_paused&&!i9100_wlan_send(&control,ethernet,64,0));
 m.sdio_regs[0x20U/4U]=0x80U;m.sdio_regs[0x4cU/4U]=0x08000004U;
 assert(!i9100_bcm_pump_tick(&pump,8)&&bus.flow_mask==8U&&pump.mailbox_count==1U);
 assert(i9100_wlan_send(&control,ethernet,64,3)==I9100_BCM_CREDIT&&!i9100_wlan_send(&control,ethernet,64,0));
 m.sdio_regs[0x20U/4U]=0x80U;m.sdio_regs[0x4cU/4U]=0x10U;unsigned old_tx=tx_bytes;
 assert(i9100_bcm_pump_tick(&pump,8)==I9100_PUMP_HALTED&&pump.fault&&bus.fault&&control.state==I9100_WLAN_FAULT);
 assert(i9100_wlan_send(&control,ethernet,64,0)!=0&&tx_bytes==old_tx);finish_pump();
 start();m.sdio_regs[0x20U/4U]=0x80U;m.sdio_regs[0x4cU/4U]=0x5000aU;
 assert(i9100_bcm_pump_tick(&pump,8)==I9100_PUMP_PROTOCOL);finish_pump();
 start();m.sdio_regs[0x20U/4U]=0x100U;assert(i9100_bcm_pump_tick(&pump,8)==I9100_PUMP_IO);finish_pump();
 start();reset_queue();uint8_t *bad=queue(80,2);bad[12]=0x20;bad[3]^=1;end_queue();
 assert(i9100_bcm_pump_tick(&pump,8)==I9100_PUMP_IO&&!pump.rx_frames&&!pump.data_frames&&packets.fault);finish_pump();
 start();reset_queue();data(1);end_queue();assert(!i9100_bcm_pump_tick(&pump,8)&&pump.dropped_frames==1U&&!pump.data_frames);finish_pump();
}
static void disconnect_and_transactions(void)
{
 linked();assert(!i9100_wlan_disconnect(&control)&&!i9100_bcm_pump_tick(&pump,8)&&control.request_command==52U);ack();
 assert(control.state==I9100_WLAN_DISCONNECTING);reset_queue();event(16,0);end_queue();
 assert(!i9100_bcm_pump_tick(&pump,8)&&control.state==I9100_WLAN_RESTORING&&control.pending);ack();
 assert(control.state==I9100_WLAN_IDLE);finish_pump();
 start();m.sdio_regs[0x20U/4U]=0x80U;m.sdio_regs[0x4cU/4U]=4U;unsigned before=m.ops;
 assert(!i9100_bcm_pump_tick(&pump,8));unsigned count=m.ops-before;finish_pump();
 for(unsigned n=1;n<=count;n++){
  start();m.sdio_regs[0x20U/4U]=0x80U;m.sdio_regs[0x4cU/4U]=4U;m.fail_op=m.ops+n;
  assert(i9100_bcm_pump_tick(&pump,8)==I9100_PUMP_IO&&pump.fault&&bus.fault&&control.state==I9100_WLAN_FAULT);
  unsigned after=m.ops;assert(i9100_bcm_pump_tick(&pump,8)==I9100_PUMP_IO&&m.ops==after);finish_pump();
 }
 start();m.locked=1;assert(i9100_bcm_pump_close(&pump)==I9100_PUMP_BUSY&&pump.initialized&&pump.closing&&bus.rx_owner==&pump);m.locked=0;finish_pump();
}
static void event_races(void)
{
 linked();reset_queue();data(1);end_queue();m.sdio_regs[0x20U/4U]=0x20U;m.post_clear_irq=0x40U;
 assert(!i9100_bcm_pump_tick(&pump,8)&&delivered==1U&&!pump.pending_rx&&!(m.sdio_regs[0x20U/4U]&0x40U));
 unsigned before=rx_wire_calls;assert(!i9100_bcm_pump_tick(&pump,8)&&rx_wire_calls==before);finish_pump();
 linked();m.sdio_regs[0x20U/4U]=0x20U;m.post_clear_irq=0x30U;
 assert(!i9100_bcm_pump_tick(&pump,8)&&bus.data_paused&&(m.sdio_regs[0x20U/4U]&0x20U));finish_pump();
 linked();m.sdio_regs[0x20U/4U]=0x80U;m.sdio_regs[0x4cU/4U]=4U;m.post_ack_irq=0x80U;m.post_ack_mailbox=0x08000004U;
 assert(!i9100_bcm_pump_tick(&pump,8)&&pump.mailbox_count==1U&&(m.sdio_regs[0x20U/4U]&0x80U));
 assert(!i9100_bcm_pump_tick(&pump,8)&&pump.mailbox_count==2U&&bus.flow_mask==8U);finish_pump();
}
int main(void)
{
 idle_and_ownership();budget_and_data();mailbox_flow_and_faults();disconnect_and_transactions();event_races();
 puts("PASS actual boot/F1/F2/CDC/WLAN pump: idle no FIFO/RX exclusive/budget carry to zero/ordered copy then wipe/global data pause/reserved control/priority flow/mailbox HALT+version/all poll transactions/parser quarantine/no replay/disconnect+mask-before-close/callback close exclusion; card+FW stand-in only");return 0;
}
