/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sensor_hid_queue.h"
static uint32_t word(const uint8_t *p)
{return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int i9100_sensor_hid_queue_push(volatile uint32_t *head,volatile uint32_t *tail,uint8_t *storage,
 uint32_t capacity,uint32_t expected,const struct i9100_sensor_hid_packet *packet,
 const struct i9100_sensor_hid_queue_io *io,uint32_t *notify,uint32_t *new_tail)
{
 if(!head||!tail||!storage||!packet||!io||!io->barrier||!io->copy||!notify||!new_tail||capacity!=4096U||
    head==tail||notify==new_tail||word(packet->bytes+12)!=1U||word(packet->bytes+16)!=44U)return I9100_SH_ARGUMENT;
 uint32_t type=word(packet->bytes+20);if(type!=13U&&type!=20U&&type!=21U)return I9100_SH_ARGUMENT;
 uint32_t h=*head,t=*tail;*notify=0;*new_tail=t;
 if(h>capacity||t>capacity||(h&63U)||(t&63U)||t!=expected)return I9100_SH_OWNER;
 io->barrier(io->cookie);uint32_t at=t,next;
 if(t>=h){if(capacity-t>=64U)next=t+64U;else if(h>64U){at=0;next=64;}else return I9100_SH_BUSY;}
 else {if(h-t<=64U)return I9100_SH_BUSY;next=t+64U;}
 const uint8_t size[4]={60,0,0,0};io->copy(io->cookie,storage+at,size,4);
 io->copy(io->cookie,storage+at+4,packet->bytes,60);io->barrier(io->cookie);*tail=next;
 io->barrier(io->cookie);*notify=(h==t||*head==t);*new_tail=next;return 0;
}
