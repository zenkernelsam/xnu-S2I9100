/* Explicit registry hierarchy facade, never a replacement production driver. */
#ifndef I9100_BLOCK_DRIVER_MODEL
#define I9100_BLOCK_DRIVER_MODEL
#include "IOBlockStorageDevice.h"
class IOMedia;
class IOBlockStorageDriver: public IOService {
protected:
 virtual IOMedia *instantiateDesiredMediaObject(){return nullptr;}
#if defined(I9100_EMMC_MATCH_ENGINE)
 bool _ejectable=false,_removable=false,_writeProtected=true;
 IOMedia *instantiateMediaObject(UInt64,UInt64,UInt32,char *);
#endif
};
#endif
