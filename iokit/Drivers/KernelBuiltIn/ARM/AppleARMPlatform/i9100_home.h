#ifndef I9100_HOME_H
#define I9100_HOME_H
#include <stdint.h>

/* Samsung U1: GPIO2 at 0x11000000, GPX3(5), active low. Preserve GPX3(4)
 * SD card detect and every other mux/pull field during setup and teardown. */
#define I9100_HOME_CON 0xc60U
#define I9100_HOME_DAT 0xc64U
#define I9100_HOME_PULL 0xc68U
static inline uint32_t i9100_home_input_con(uint32_t con) {return con&~(15U<<20);}
static inline uint32_t i9100_home_input_pull(uint32_t pull) {return (pull&~(3U<<10))|(3U<<10);}
static inline uint32_t i9100_home_restore_con(uint32_t con,uint32_t saved)
{return (con&~(15U<<20))|(saved&(15U<<20));}
static inline uint32_t i9100_home_restore_pull(uint32_t pull,uint32_t saved)
{return (pull&~(3U<<10))|(saved&(3U<<10));}
static inline unsigned i9100_home_down(uint32_t dat) {return (dat&(1U<<5))?0U:1U;}

/* IOHIDFamily-503: 28-byte queue header + 24-byte Keyboard event (type 3).
 * The physical Home switch supplies Consumer Menu, page 0x0c / usage 0x40. */
struct I9100KeyboardEvent {
 uint32_t size,type,options;
 uint8_t depth,reserved[3];
 uint16_t usagePage,usage;
 uint32_t down;
};
struct __attribute__((packed)) I9100HomePacket {
 LeoHIDHeader header;
 I9100KeyboardEvent key;
};
typedef char I9100HomePacket52[sizeof(I9100HomePacket)==52?1:-1];
typedef char I9100HomeUsageOffset[__builtin_offsetof(I9100HomePacket,key.usagePage)==44?1:-1];
static inline bool i9100_home_packet(unsigned down,uint64_t time,uint64_t sender,I9100HomePacket *p)
{
 if(!p || down>1 || !time || !sender)return false;
 p->header.attributeLength=0;p->key.depth=0;
 p->key.reserved[0]=p->key.reserved[1]=p->key.reserved[2]=0;
 p->header.timeStamp=time;p->header.senderID=sender;p->header.eventCount=1;
 p->header.options=1;p->key.size=24;p->key.type=3;p->key.options=1;
 p->key.usagePage=12;p->key.usage=0x40;p->key.down=down;
 return true;
}
struct I9100HomeEdge { uint64_t timestamp; unsigned down; };
struct I9100HomeState {
 unsigned initialized,armed,stable,candidate,head,count,overflows;
 uint64_t candidateSince;
 I9100HomeEdge edges[16];
};
static inline void i9100_home_reset(I9100HomeState *s)
{
 s->initialized=s->armed=s->stable=s->candidate=s->head=s->count=s->overflows=0;
 s->candidateSince=0;
}
static inline void i9100_home_sample_period(I9100HomeState *s,unsigned down,uint64_t now,uint64_t debounce)
{
 if(down>1 || !now)return;
 if(!s->initialized){s->initialized=1;s->stable=s->candidate=down;s->candidateSince=now;s->armed=!down;return;}
 if(down!=s->candidate){s->candidate=down;s->candidateSince=now;return;}
 if(now-s->candidateSince<debounce)return;
 if(!s->armed){if(!down){s->stable=0;s->armed=1;}return;}
 if(s->stable==down)return;
 if(s->count==16){if(s->overflows!=UINT32_MAX)s->overflows++;return;}
 unsigned tail=(s->head+s->count)&15U;
 s->edges[tail].timestamp=s->candidateSince;s->edges[tail].down=down;
 s->count++;s->stable=down;
}
static inline void i9100_home_sample(I9100HomeState *s,unsigned down,uint64_t now)
{i9100_home_sample_period(s,down,now,20000000ULL);}
#endif
