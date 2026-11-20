/* Explicit host thread/gate facade, not a substitute for kernel delivery. */
#ifndef I9100_EMMC_WORKLOOP_FACADE_H
#define I9100_EMMC_WORKLOOP_FACADE_H
#include "IOEventSource.h"
#include <condition_variable>
#include <mutex>
#include <thread>
class IOWorkLoop : public OSObject {
public:
 struct Impl;Impl *impl=nullptr;
 static IOWorkLoop *workLoop();
 bool init()override;
 IOReturn addEventSource(IOEventSource *);IOReturn removeEventSource(IOEventSource *);
 void openGate();void closeGate();bool onThread()const;void signalWorkAvailable();
 void free() override;
 void finishModelThread();
 bool deferred=false;bool deferDeletion()const override{return deferred;}
};
#endif
