/* SPDX-License-Identifier: BSD-2-Clause; host work-loop facade, never ARM SDK. */
#ifndef I9100_MODEL_EVENTS_H
#define I9100_MODEL_EVENTS_H
#include "../i9100_model_iokit.h"
typedef int IOReturn;typedef unsigned UInt32;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=-1};
void *i9100_model_event_allocate(size_t);void i9100_model_event_free(void *);
class OSObject {
 unsigned refs=1;
public:
 static void *operator new(size_t size) noexcept{return i9100_model_event_allocate(size);}
 static void operator delete(void *p){i9100_model_event_free(p);}
 virtual ~OSObject()=default;virtual bool init(){return true;}virtual void free(){}
 void retain(){__sync_add_and_fetch(&refs,1);}void release(){if(!__sync_sub_and_fetch(&refs,1)){free();delete this;}}
};
#define OSDeclareDefaultStructors(name) public: name(); virtual ~name(); private:
#define OSDefineMetaClassAndStructors(name,base) name::name()=default;name::~name()=default;
class IOWorkLoop;
class IOEventSource : public OSObject {
protected:
 OSObject *owner=nullptr;bool enabled=true;IOWorkLoop *workLoop=nullptr;
 void signalWorkAvailable();
public:
 typedef void (*Action)(OSObject *,IOEventSource *);
 using OSObject::init;
 virtual bool init(OSObject *o,Action=0){owner=o;return true;}
 virtual bool checkForWork(){return false;}
 virtual void enable(){enabled=true;}virtual void disable(){enabled=false;}
 virtual void setWorkLoop(IOWorkLoop *w){workLoop=w;}
};
class IOTimerEventSource : public IOEventSource {
public:
 typedef void (*Action)(OSObject *,IOTimerEventSource *);
 Action callback=nullptr;bool queued=false;unsigned generation=0;
 static IOTimerEventSource *timerEventSource(OSObject *o,Action a){auto *t=new IOTimerEventSource;if(t){t->owner=o;t->callback=a;}return t;}
 IOReturn setTimeoutUS(UInt32);void cancelTimeout(){queued=false;generation++;}
 void disable() override{IOEventSource::disable();cancelTimeout();}
 void fire(){if(queued&&enabled){queued=false;callback(owner,this);}}
};
class IOWorkLoop : public OSObject {
public:
 typedef IOReturn (*Action)(OSObject *,void *,void *,void *,void *);
 IOEventSource *sources[4]={};unsigned count=0;bool pending=false,thread=false,gate=false;
 static IOWorkLoop *workLoop();
 IOReturn addEventSource(IOEventSource *);IOReturn removeEventSource(IOEventSource *);
 IOReturn runAction(Action,OSObject *,void *a=0,void *b=0,void *c=0,void *d=0);
 bool onThread() const{return thread;}
 bool inGate() const{return gate;}
 void pump();void tick();void free() override;
};
#endif
