/* Host scheduler boundary; production uses original XNU IOEventSource. */
#ifndef I9100_EMMC_EVENT_FACADE_H
#define I9100_EMMC_EVENT_FACADE_H
#include "../emmc_model.h"
class IOWorkLoop;
class IOEventSource : public OSObject {
protected:
 OSObject *owner=nullptr;bool enabled=true;IOWorkLoop *workLoop=nullptr;
 void signalWorkAvailable();
 void openGate();void closeGate();
public:
 using OSObject::init;
 typedef void (*Action)(OSObject *,...);
 virtual bool init(OSObject *o,Action=0){owner=o;return o!=nullptr;}
 virtual bool checkForWork(){return false;}
 virtual void enable(){enabled=true;}
 virtual void disable(){enabled=false;}
 virtual void setWorkLoop(IOWorkLoop *w){workLoop=w;}
 virtual IOWorkLoop *getWorkLoop()const{return workLoop;}
};
#endif
