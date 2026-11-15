/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_uart_bluetooth.h"
#include "i9100_pwm_haptic.h"
struct model {
 uint32_t regs[32],locked,reads,writes,fail_write,freeze;
 uint64_t ns;unsigned tx_count,tx_full_after,rx_head,rx_tail;
 uint8_t tx[5000],rx[5000];
};
static struct model m;
static int lock(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint64_t now(void *c){return ((struct model *)c)->ns;}
static int delay(void *c,uint32_t us){struct model *p=c;assert(p->locked);if(!p->freeze)p->ns+=us*1000ULL;return 0;}
static uint32_t read32(void *c,unsigned off)
{
 struct model *p=c;assert(p->locked&&off<128&&!(off&3));p->reads++;
 if(off==24){uint32_t v=p->regs[off/4];
  if(p->tx_full_after&&p->tx_count>=p->tx_full_after)v|=1U<<24;
  unsigned remaining=p->rx_tail-p->rx_head;if(remaining>=256U)v|=256U;else v|=remaining;
  return v;}
 return p->regs[off/4];
}
static void write32(void *c,unsigned off,uint32_t v)
{struct model *p=c;assert(p->locked&&off<128&&!(off&3));if(++p->writes!=p->fail_write)p->regs[off/4]=v;}
static uint8_t read8(void *c,unsigned off)
{struct model *p=c;assert(p->locked&&off==36&&p->rx_head<p->rx_tail);return p->rx[p->rx_head++];}
static void write8(void *c,unsigned off,uint8_t v)
{struct model *p=c;assert(p->locked&&off==32&&p->tx_count<5000);p->tx[p->tx_count++]=v;}
static struct i9100_uart_io uart_io={&m,read32,read8,write32,write8,lock,unlock,now,delay};
static void uart(void)
{
 for(unsigned mode=1;mode<=2;mode++){
  memset(&m,0,sizeof(m));m.regs[4]=4;m.regs[14]=15;m.regs[1]=0x405;
  struct i9100_uart_bluetooth u={0};assert(!i9100_uart_bt_init(&u,&uart_io));assert(!m.reads&&!m.writes);
  assert(!i9100_uart_bt_enable(&u,1));
  assert(i9100_uart_bt_configure(&u,48000000,115200,mode,0)==I9100_UART_ARGUMENT&&!m.writes);
  assert(!i9100_uart_bt_configure(&u,48000000,115200,mode,1));
  assert(u.actual_baud==48000000U/417U&&m.regs[10]==25U&&m.regs[11]==(mode==1?1U:0x80U));
  assert(m.regs[1]==0x405U&&m.regs[3]==16U);
  const uint8_t command[]={1,3,0x0c,0};unsigned sent=99;
  assert(!i9100_uart_bt_write(&u,command,4,&sent)&&sent==4&&!memcmp(m.tx,command,4));
  memcpy(m.rx,command,4);m.rx_tail=4;uint8_t bytes[8];unsigned received;
  assert(!i9100_uart_bt_read(&u,bytes,8,&received)&&received==4&&!memcmp(bytes,command,4));
  m.tx_full_after=m.tx_count+2;m.freeze=1;
  assert(i9100_uart_bt_write(&u,command,4,&sent)==I9100_UART_TIMEOUT&&sent==2&&u.fault);
  assert(i9100_uart_bt_write(&u,command,4,&sent)==I9100_UART_IO&&!sent);
  m.regs[4]=0;assert(i9100_uart_bt_stop(&u)==I9100_UART_TIMEOUT&&u.prepared);
  m.regs[4]=4;assert(!i9100_uart_bt_stop(&u)&&!u.prepared&&u.fault&&m.regs[1]==0x405U);
 }
 memset(&m,0,sizeof(m));m.regs[4]=4;m.regs[14]=15;struct i9100_uart_bluetooth u={0};
 assert(!i9100_uart_bt_init(&u,&uart_io));assert(!i9100_uart_bt_enable(&u,1));
 assert(!i9100_uart_bt_configure(&u,48000000,3000000,1,1)&&u.actual_baud==3000000&&m.regs[10]==0);
 m.rx[0]=0xab;m.rx_tail=1;m.regs[5]=4;uint8_t out;unsigned got;
 assert(i9100_uart_bt_read(&u,&out,1,&got)==I9100_UART_RX_ERROR&&!got&&u.rx_errors==1);
 for(unsigned fault=1;fault<=11;fault++){
  memset(&m,0,sizeof(m));m.regs[4]=4;m.regs[14]=15;memset(&u,0,sizeof(u));
  assert(!i9100_uart_bt_init(&u,&uart_io));assert(!i9100_uart_bt_enable(&u,1));m.fail_write=fault;
  int e=i9100_uart_bt_configure(&u,48000000,115200,1,1);
  if(e)assert(u.prepared&&u.fault);m.fail_write=0;assert(!i9100_uart_bt_stop(&u));
 }
}
static uint32_t pwm_read(void *c,unsigned off){struct model *p=c;assert(p->locked&&off<128);p->reads++;return p->regs[off/4];}
static struct i9100_pwm_io pwm_io={&m,pwm_read,write32,lock,unlock};
static void pwm(void)
{
 const unsigned prescalers[]={0,9,99};
 for(unsigned n=0;n<3;n++){
  memset(&m,0,sizeof(m));m.regs[0]=0xabcd0000|prescalers[n];m.regs[1]=0x33333533;m.regs[2]=0x12340001;
  struct i9100_pwm_haptic p={0};assert(!i9100_pwm_haptic_init(&p,&pwm_io,100000000));assert(!m.reads&&!m.writes);
  assert(!i9100_pwm_haptic_enable(&p,1));
  assert(!i9100_pwm_haptic_configure(&p,38022,20912,1));
  uint32_t ticks=p.count+1U,high=p.count-p.compare;
  assert((uint64_t)high*2U>=ticks&&(uint64_t)high*100U<=(uint64_t)ticks*55U);
  assert(m.regs[0]==(0xabcd0000|prescalers[n])&&m.regs[1]==0x33333503U);
  assert((m.regs[2]&~0xf00U)==0x12340001U&&!((m.regs[2]>>9)&1U));
  assert(!i9100_pwm_haptic_run(&p,1)&&(m.regs[2]&0x100));
  m.regs[0]++;assert(i9100_pwm_haptic_run(&p,1)==I9100_PWM_CLOCK&&p.fault&&!(m.regs[2]&0x100));
  assert(!i9100_pwm_haptic_release(&p)&&!p.prepared&&m.regs[1]==0x33333533U&&m.regs[2]==0x12340001U);
 }
 memset(&m,0,sizeof(m));struct i9100_pwm_haptic p={0};
 assert(!i9100_pwm_haptic_init(&p,&pwm_io,100000000));assert(!i9100_pwm_haptic_enable(&p,1));
 assert(i9100_pwm_haptic_configure(&p,38022,37641,1)==I9100_PWM_ARGUMENT&&!m.writes);
 m.regs[2]=0x100;assert(i9100_pwm_haptic_configure(&p,38022,19011,1)==I9100_PWM_BUSY);
 m.regs[2]=0;m.fail_write=2;assert(i9100_pwm_haptic_configure(&p,38022,19011,1)==I9100_PWM_IO&&p.fault);
 m.fail_write=0;assert(!i9100_pwm_haptic_release(&p));
}
int main(void)
{uart();pwm();puts("PASS UART fraction/baud/RTS-CTS/partial TX/frozen clock/RX errors/restore; PWM1 downcounter/duty cap/shared-prescaler/foreign fields/stop/fault");return 0;}
