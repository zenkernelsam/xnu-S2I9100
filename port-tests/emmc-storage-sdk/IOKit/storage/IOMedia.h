/* Exact role/base/writable properties used by actual storage provider. */
#ifndef I9100_MEDIA_MODEL_H
#define I9100_MEDIA_MODEL_H
#include "IOStorage.h"
typedef unsigned IOMediaAttributeMask;
class IOMedia : public IOService {
public:
 using IOService::init;
 UInt64 base=0,size=0;bool whole=false,writable=false;
 bool isWhole()const{return whole;}bool isWritable()const{return writable;}UInt64 getSize()const{return size;}UInt64 getBase()const{return base;}
#if defined(I9100_EMMC_MATCH_ENGINE)
 bool matchPropertyTable(OSDictionary *,SInt32 *)override;
#endif
 bool init(UInt64 b,UInt64 s,unsigned block,unsigned,bool w,bool rw,const char *hint)
 {assert(block==512&&s&&hint);base=b;size=s;whole=w;writable=rw;
  setProperty("Whole",w);setProperty("Writable",rw);setProperty("Size",s,64);
  setProperty("Content Hint",hint);setProperty("Preferred Block Size",block,64);return true;}
};
#endif
