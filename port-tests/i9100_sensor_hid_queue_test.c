/* SPDX-License-Identifier: BSD-2-Clause; actual 60-byte shared queue algorithm. */
#include "i9100_sensor_hid_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned barriers,copies;
static void barrier(void *p){(void)p;barriers++;}
static void copy(void *p,void *out,const void *in,uint32_t n){(void)p;copies++;memcpy(out,in,n);}
static uint8_t guarded[4128];
static void guards(void){for(unsigned n=0;n<16;n++)assert(guarded[n]==0xa5&&guarded[4112+n]==0xa5);}
int main(void)
{
 struct i9100_sensor_sample sample={1,1,1,8,{1000000,-1000000,0}};
 struct i9100_sensor_hid_packet packet;assert(!i9100_sensor_hid_encode(&sample,3,7,&packet));
 memset(guarded,0xa5,sizeof(guarded));uint8_t *storage=guarded+16;
 volatile uint32_t head=0,tail=0;uint32_t expected=0,notify=0,next=0;
 struct i9100_sensor_hid_queue_io io={0,barrier,copy};
 for(unsigned n=0;n<64;n++){
  assert(!i9100_sensor_hid_queue_push(&head,&tail,storage,4096,expected,&packet,&io,&notify,&next));
  expected=next;assert(next==(n+1U)*64U&&notify==(n==0));guards();
 }
 unsigned before=copies;assert(i9100_sensor_hid_queue_push(&head,&tail,storage,4096,expected,&packet,&io,&notify,&next)==I9100_SH_BUSY&&copies==before);
 head=128;assert(!i9100_sensor_hid_queue_push(&head,&tail,storage,4096,expected,&packet,&io,&notify,&next)&&tail==64);expected=next;
 assert(!memcmp(storage+4,packet.bytes,60));guards();
 for(unsigned n=0;n<400;n++){
  head=tail;assert(!i9100_sensor_hid_queue_push(&head,&tail,storage,4096,expected,&packet,&io,&notify,&next)&&notify);
  expected=next;guards();
 }
 before=copies;head=4097;assert(i9100_sensor_hid_queue_push(&head,&tail,storage,4096,expected,&packet,&io,&notify,&next)==I9100_SH_OWNER&&copies==before);
 head=1;assert(i9100_sensor_hid_queue_push(&head,&tail,storage,4096,expected,&packet,&io,&notify,&next)==I9100_SH_OWNER&&copies==before);
 head=0;tail=3;assert(i9100_sensor_hid_queue_push(&head,&tail,storage,4096,expected,&packet,&io,&notify,&next)==I9100_SH_OWNER&&copies==before);
 tail=0;assert(i9100_sensor_hid_queue_push(&head,&tail,storage,4096,64,&packet,&io,&notify,&next)==I9100_SH_OWNER&&copies==before);
 assert(i9100_sensor_hid_queue_push(&head,&tail,storage,8192,0,&packet,&io,&notify,&next)==I9100_SH_ARGUMENT&&copies==before);
 guards();assert(barriers);
 puts("PASS actual sensor shared queue: full64, reserved wrap slot,400 consumed wraps, exact60+4 bytes, corrupt/unaligned head/tail and changed private tail rejected before copy, canaries intact");return 0;
}
