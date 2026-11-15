/* SPDX-License-Identifier: BSD-2-Clause
 * Independent function-level SDIO/FIFO model; the actual host/card protocol
 * has its own register test in i9100_wifi_bluetooth_test.c. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_bcm_f2.h"
struct model {
 unsigned locked,calls,fail_call,bad_ready,bad_size,rx_cursor,rx_length,tx_length;
 uint8_t rx[4096],tx[4096];
 uint32_t lengths[16],writes[16];
};
static struct model m;
static struct i9100_bcm_f2 transport;
static struct i9100_sdio card;
static int lock(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint64_t now(void *c){(void)c;return 12345678;}
static int delay(void *c,uint32_t us){(void)c;(void)us;return 0;}
int i9100_sdio_cmd52(struct i9100_sdio *s,int write,unsigned fn,uint32_t address,uint8_t *value)
{
 assert(s==&card&&m.locked&&!write&&fn==0U);m.calls++;
 if(m.calls==m.fail_call){s->fault=1;return I9100_SDIO_CRC;}
 switch(address){case 2:*value=6;break;case 3:*value=m.bad_ready?2:6;break;
 case 0x210:*value=0;break;case 0x211:*value=m.bad_size?1:2;break;default:assert(0);}
 return 0;
}
int i9100_sdio_cmd53(struct i9100_sdio *s,int write,unsigned fn,uint32_t address,int increment,uint8_t *bytes,uint32_t length)
{
 assert(s==&card&&m.locked&&fn==2&&address==0x8000&&!increment&&length<=2048U);
 assert(!(length&3U));assert(length<=512U||!(length&511U));
 unsigned call=m.calls++;assert(call<16);m.lengths[call]=length;m.writes[call]=write;
 if(m.calls==m.fail_call){s->fault=1;return I9100_SDIO_CRC;}
 if(write){assert(m.tx_length+length<=sizeof(m.tx));memcpy(m.tx+m.tx_length,bytes,length);m.tx_length+=length;}
 else{assert(m.rx_cursor+length<=m.rx_length);memcpy(bytes,m.rx+m.rx_cursor,length);m.rx_cursor+=length;}
 return 0;
}
static struct i9100_f2_io io={&m,lock,unlock,now,delay};
static void init(void)
{
 memset(&m,0,sizeof(m));memset(&card,0,sizeof(card));memset(&transport,0,sizeof(transport));
 card.initialized=card.active=1;card.block_size[1]=64;card.block_size[2]=512;
 assert(!i9100_bcm_f2_init(&transport,&card,&io)&&!m.calls);
 assert(i9100_bcm_f2_enable(&transport,1,0)==I9100_F2_GENERATION&&!m.calls);
 assert(!i9100_bcm_f2_enable(&transport,1,1));m.calls=0;
}
static void header(uint8_t *bytes,unsigned length)
{
 bytes[0]=(uint8_t)length;bytes[1]=(uint8_t)(length>>8);
 uint16_t inverse=(uint16_t)~length;bytes[2]=(uint8_t)inverse;bytes[3]=(uint8_t)(inverse>>8);
 bytes[4]=7;bytes[5]=2;bytes[7]=12;bytes[9]=9;
}
static uint8_t output[2048],frame[2048];
static void send_tests(void)
{
 const struct test {unsigned logical,padded,wire;} tests[]={{12,12,12},{33,36,36},{511,512,512},{513,516,1024},{1500,1500,1536},{2048,2048,2048}};
 for(unsigned n=0;n<sizeof(tests)/sizeof(tests[0]);n++){
  init();struct i9100_bcm_io bcm;assert(!i9100_bcm_f2_get_io(&transport,&bcm));
  memset(frame,0,sizeof(frame));for(unsigned b=12;b<tests[n].logical;b++)frame[b]=(uint8_t)b;
  header(frame,tests[n].logical);assert(!bcm.claim(bcm.cookie));
  assert(!bcm.send_frame(bcm.cookie,frame,tests[n].padded));bcm.release(bcm.cookie);
  assert(m.calls==1&&m.tx_length==tests[n].wire&&!memcmp(m.tx,frame,tests[n].padded));
  for(unsigned b=tests[n].padded;b<tests[n].wire;b++)assert(!m.tx[b]);
  assert(transport.tx_frames==1&&transport.logical_bytes==tests[n].logical&&transport.wire_bytes==tests[n].wire);
 }
 init();struct i9100_bcm_io bcm;assert(!i9100_bcm_f2_get_io(&transport,&bcm));header(frame,32);
 m.fail_call=1;assert(!bcm.claim(bcm.cookie));assert(bcm.send_frame(bcm.cookie,frame,32)==I9100_F2_IO);bcm.release(bcm.cookie);
 assert(transport.fault&&!transport.tx_frames&&card.fault);assert(!i9100_bcm_f2_enable(&transport,0,0));
 assert(i9100_bcm_f2_enable(&transport,1,1)==I9100_F2_GENERATION);
}
static void receive_tests(void)
{
 const struct test {unsigned logical,wire,body;} tests[]={{12,32,0},{32,32,0},{33,36,4},{544,544,512},{545,1056,1024},{2048,2080,2048}};
 for(unsigned n=0;n<sizeof(tests)/sizeof(tests[0]);n++){
  init();memset(m.rx,0,sizeof(m.rx));for(unsigned b=12;b<tests[n].logical;b++)m.rx[b]=(uint8_t)(b*13U);
  header(m.rx,tests[n].logical);m.rx_length=tests[n].wire;memset(output,0xa5,sizeof(output));unsigned count=99;
  assert(!i9100_bcm_f2_receive(&transport,output,sizeof(output),&count)&&count==tests[n].logical);
  assert(m.lengths[0]==32U&&m.rx_cursor==tests[n].wire&&m.calls==(tests[n].body?2U:1U));
  if(tests[n].body)assert(m.lengths[1]==tests[n].body);
  assert(!memcmp(output,m.rx,tests[n].logical));for(unsigned b=count;b<sizeof(output);b++)assert(output[b]==0xa5);
  assert(transport.rx_frames==1&&transport.wire_bytes==tests[n].wire);
 }
 init();memset(m.rx,0,32);m.rx_length=32;memset(output,0xa5,sizeof(output));unsigned count;
 assert(!i9100_bcm_f2_receive(&transport,output,sizeof(output),&count)&&!count&&transport.empty_reads==1);
 for(unsigned b=0;b<sizeof(output);b++)assert(output[b]==0xa5);
 for(unsigned fault=1;fault<=2;fault++){
  init();memset(m.rx,0,sizeof(m.rx));header(m.rx,545);m.rx_length=1056;m.fail_call=fault;memset(output,0xa5,sizeof(output));
  assert(i9100_bcm_f2_receive(&transport,output,sizeof(output),&count)==I9100_F2_IO&&!count&&transport.fault);
  for(unsigned b=0;b<sizeof(output);b++)assert(output[b]==0xa5);
 }
 init();memset(m.rx,0,32);header(m.rx,2049);m.rx_length=32;
 assert(i9100_bcm_f2_receive(&transport,output,sizeof(output),&count)==I9100_F2_FRAME&&m.calls==1);
 init();memset(m.rx,0,32);header(m.rx,32);m.rx[3]^=1;m.rx_length=32;
 assert(i9100_bcm_f2_receive(&transport,output,sizeof(output),&count)==I9100_F2_FRAME&&!count);
 init();memset(m.rx,0,32);header(m.rx,32);m.rx[5]=3;m.rx_length=32;
 assert(i9100_bcm_f2_receive(&transport,output,sizeof(output),&count)==I9100_F2_UNSUPPORTED&&!count);
 init();unsigned calls=m.calls;assert(i9100_bcm_f2_receive(&transport,output,64,&count)==I9100_F2_ARGUMENT&&m.calls==calls);
}
static void gate_tests(void)
{
 init();assert(!i9100_bcm_f2_enable(&transport,0,0));m.bad_ready=1;
 assert(i9100_bcm_f2_enable(&transport,1,2)==I9100_F2_DISABLED&&!transport.enabled);
 m.bad_ready=0;m.bad_size=1;assert(i9100_bcm_f2_enable(&transport,1,2)==I9100_F2_DISABLED);
 m.bad_size=0;assert(!i9100_bcm_f2_enable(&transport,1,2));assert(transport.generation==2);
}
int main(void)
{send_tests();receive_tests();gate_tests();puts("PASS F2 fixed8000/4-byte/512B block framing/header32/atomic publish/zero-end/malformed/CRC/fresh-generation gates");return 0;}
