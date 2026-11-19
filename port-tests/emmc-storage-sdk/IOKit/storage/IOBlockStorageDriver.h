/* Explicit registry hierarchy facade, never a replacement production driver. */
#ifndef I9100_BLOCK_DRIVER_MODEL
#define I9100_BLOCK_DRIVER_MODEL
#include "IOBlockStorageDevice.h"
class IOBlockStorageDriver: public IOService {};
#endif
