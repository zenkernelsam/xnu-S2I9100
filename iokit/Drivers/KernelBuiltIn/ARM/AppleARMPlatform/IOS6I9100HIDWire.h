#ifndef IOS6_I9100_HID_WIRE_H
#define IOS6_I9100_HID_WIRE_H
#include <stdint.h>

/* iOS6 10B329 IOHIDEventCreateData/CreateWithData: 16-byte serialized
 * prefix; event options/depth/sender are at offsets 16/20/24 in data.
 * This is distinct from the iOS7 queue prefix, not a header-size guess. */
struct IOS6HIDHeader { uint64_t timestamp; uint32_t options,count; };
struct IOS6HIDBase {
 uint32_t size,type;
 uint64_t reservedTimestamp;
 uint32_t options;
 uint8_t depth,reserved[3];
 uint64_t sender;
};
struct IOS6HIDKeyboard { IOS6HIDBase base; uint16_t page,usage; uint32_t down; };
struct IOS6HIDDigitizer {
 IOS6HIDBase base;
 int32_t x,y,z;
 uint32_t index,transducerType,identity,eventMask,childEventMask,buttons;
 int32_t pressure,auxiliaryPressure,twist;
 uint32_t orientationType;
 int32_t quality,density,irregularity,majorRadius,minorRadius;
};
struct IOS6HomePacket { IOS6HIDHeader header; IOS6HIDKeyboard key; };
struct IOS6TouchPacket { IOS6HIDHeader header; IOS6HIDDigitizer hand,finger; };
typedef char IOS6Header16[sizeof(IOS6HIDHeader)==16?1:-1];
typedef char IOS6Base32[sizeof(IOS6HIDBase)==32?1:-1];
typedef char IOS6Keyboard40[sizeof(IOS6HIDKeyboard)==40?1:-1];
typedef char IOS6Digitizer104[sizeof(IOS6HIDDigitizer)==104?1:-1];
typedef char IOS6Home56[sizeof(IOS6HomePacket)==56?1:-1];
typedef char IOS6Touch224[sizeof(IOS6TouchPacket)==224?1:-1];
typedef char IOS6KeyboardUsage48[__builtin_offsetof(IOS6HomePacket,key.page)==48?1:-1];
typedef char IOS6FingerDepth[__builtin_offsetof(IOS6TouchPacket,finger.base.depth)==140?1:-1];

static inline void ios6_hid_base(IOS6HIDBase *p,uint32_t size,uint32_t type,
 uint32_t options,uint8_t depth,uint64_t sender)
{
 p->size=size;p->type=type;p->reservedTimestamp=0;p->options=options;
 p->depth=depth;p->reserved[0]=p->reserved[1]=p->reserved[2]=0;p->sender=sender;
}
static inline bool ios6_home_wire(const I9100HomePacket *src,IOS6HomePacket *out)
{
 if(!src || !out || !src->header.timeStamp || !src->header.senderID ||
  src->key.type!=3 || src->key.size!=24 || src->key.down>1)return false;
 out->header.timestamp=src->header.timeStamp;out->header.options=src->header.options;out->header.count=1;
 ios6_hid_base(&out->key.base,40,3,src->key.options,0,src->header.senderID);
 out->key.page=src->key.usagePage;out->key.usage=src->key.usage;out->key.down=src->key.down;
 return true;
}
static inline void ios6_digitizer_wire(const uint8_t *src,IOS6HIDDigitizer *out,uint64_t sender)
{
 uint32_t options=(uint32_t)src[8]|((uint32_t)src[9]<<8)|((uint32_t)src[10]<<16)|((uint32_t)src[11]<<24);
 ios6_hid_base(&out->base,104,11,options,src[12],sender);
 /* Read the packed source as bytes, without imposing uint64/struct
  * alignment on a class member. The 72-byte digitizer payload is unchanged. */
 uint8_t *bytes=(uint8_t *)out;
 for(unsigned n=0;n<72;n++)bytes[32+n]=src[16+n];
 /* iOS6 IOHIDEventCreateDigitizerFingerEvent passes 0x22, and its
  * public enum defines Hand=0x23. The iOS7 producer's 2/3 are not these IDs. */
 if(out->transducerType==2 || out->transducerType==3)out->transducerType+=0x20;
}
static inline bool ios6_touch_wire(const LeoHIDPacket *src,IOS6TouchPacket *out)
{
 if(!src || !out || !src->header.timeStamp || !src->header.senderID ||
  src->header.eventCount!=2 || src->hand.type!=11 || src->finger.type!=11 ||
  src->hand.size!=88 || src->finger.size!=88)return false;
 out->header.timestamp=src->header.timeStamp;out->header.options=src->header.options;out->header.count=2;
 ios6_digitizer_wire((const uint8_t *)src+__builtin_offsetof(LeoHIDPacket,hand),&out->hand,src->header.senderID);
 ios6_digitizer_wire((const uint8_t *)src+__builtin_offsetof(LeoHIDPacket,finger),&out->finger,src->header.senderID);
 return true;
}
static inline bool ios6_home_match(const void *bytes,uint32_t size)
{
 if(!bytes || size!=56)return false;
 const IOS6HomePacket *p=(const IOS6HomePacket *)bytes;
 return p->header.count==1 && p->key.base.size==40 && p->key.base.type==3 &&
  p->key.base.depth==0 && p->key.page==12 && p->key.usage==0x40;
}
#endif
