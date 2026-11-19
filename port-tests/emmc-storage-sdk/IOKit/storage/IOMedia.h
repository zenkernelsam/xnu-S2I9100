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
 bool init(UInt64 b,UInt64 s,unsigned block,unsigned,bool w,bool rw,const char *hint)
 {assert(block==512&&s&&hint);base=b;size=s;whole=w;writable=rw;return true;}
};
#endif
