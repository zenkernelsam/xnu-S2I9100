/* Host-only owned descriptor fixture; production uses actual XNU OSObject. */
#ifndef I9100_MODEL_BUFFER_DESCRIPTOR_H
#define I9100_MODEL_BUFFER_DESCRIPTOR_H
#include "../i9100_model_iokit.h"
class IOBufferMemoryDescriptor : public IOMemoryDescriptor {
public:
 unsigned refs;void *data;
 static IOBufferMemoryDescriptor *withCapacity(size_t,unsigned,bool);
 void *getBytesNoCopy(){return data;}
 void retain(){refs++;}
 void release();
};
#endif
