#!/usr/bin/env python3
"""Exercise actual display client methods with shared recursive gate facades.

Extracts the production methods, rather than rebuilding their control logic.
IOKit locks/timers, IPC ports and FIMD are modeled; no phone operations.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import hashlib,json,re,subprocess

repo=Path(__file__).resolve().parents[1]
platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
source=platform/'IOS7LeoDisplay.cpp';text=source.read_text();before=hashlib.sha256(source.read_bytes()).hexdigest()
out=repo/'out'/('display-vsync-power-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
names=('applyVsyncTimer','vsyncWorkAction','displayPowerMembershipGated','observeVsyncPowerGated',
    'setDisplayPowerGated','stopVsync','stopVsyncGated','emitVsync','emitVsyncGated',
    'registerNotificationPort','registerNotificationPortGated','setVsyncCallbackGated','free','clientClose')
methods=[]
for name in names:
    match=re.search(r'^(?:IOReturn|void) IOS7LabFramebufferClient::'+name+r'\(',text,re.M)
    assert match,name
    start=match.start();begin=text.index('{',match.end());depth=1;end=begin+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    methods.append(text[start:end])
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#define BOARD_CONFIG_EXYNOS4210_I9100 1
#define bzero(ptr,n) memset(ptr,0,n)
using IOReturn=int;using UInt32=uint32_t;using OSObject=struct Object;
using mach_port_t=uint32_t;using io_user_reference_t=uint64_t;using AbsoluteTime=uint64_t;
using OSAsyncReference64=uint64_t[3];
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotReady=2,kIOReturnNotOpen=3,
 kIOReturnUnsupported=4,kIOReturnBusy=5,MACH_PORT_NULL=0,MACH_SEND_TIMED_OUT=100,MACH_SEND_NO_BUFFER=101};
enum {LEO_VSYNC_PORT=1,LEO_VSYNC_CALLBACK=2,LEO_VSYNC_EMIT=3,LEO_VSYNC_CLOSE=4,LEO_VSYNC_POWER=5,LEO_VSYNC_JOIN=6,LEO_VSYNC_LEAVE=7};
enum {LEO_GRAPHICS_REASON_TIMER,LEO_GRAPHICS_REASON_CLOSED,LEO_GRAPHICS_REASON_PORT_COPY,
 LEO_GRAPHICS_REASON_NONE,LEO_GRAPHICS_REASON_ARGUMENT,LEO_GRAPHICS_EVENT_TIMER_FAILURE,
 LEO_GRAPHICS_EVENT_CLOSE,LEO_GRAPHICS_EVENT_COPY_SEND_FAILURE,LEO_GRAPHICS_EVENT_SEND_FAILURE,
 LEO_GRAPHICS_EVENT_STALE_SEND,LEO_GRAPHICS_EVENT_PORT};
static thread_local unsigned lockDepth,gateDepth;
struct Object {unsigned retains=0;bool released=false;void retain(){++retains;}virtual void release(){assert(retains);--retains;}};
struct ModelLock {std::mutex value;};
static void IOLockLock(ModelLock*p){p->value.lock();++lockDepth;}
static void IOLockUnlock(ModelLock*p){assert(lockDepth);--lockDepth;p->value.unlock();}
static void IOLockFree(ModelLock*p){assert(!lockDepth);delete p;}
struct IOWorkLoop:Object {std::recursive_mutex gate;
 IOReturn runAction(IOReturn(*fn)(Object*,void*,void*,void*,void*),Object*p,void*arg){
  assert(!lockDepth);std::lock_guard<std::recursive_mutex> hold(gate);++gateDepth;
  IOReturn result=fn(p,arg,nullptr,nullptr,nullptr);--gateDepth;return result;}
 void removeEventSource(Object*){assert(!lockDepth);}
};
struct IOTimerEventSource:Object {bool armed=false;unsigned arms=0,cancels=0;bool fail=false;
 void release() override {assert(retains);if(!--retains)delete this;}
 IOReturn setTimeoutUS(unsigned value){assert(gateDepth && !lockDepth && value==16667);++arms;if(fail)return 77;armed=true;return 0;}
 void cancelTimeout(){assert(gateDepth && !lockDepth);++cancels;armed=false;}
};
#define IPC_PORT_VALID(port) ((port)!=0)
struct LeoVsyncAction {uint32_t operation;mach_port_t port;UInt32 type;io_user_reference_t refcon,callback;IOTimerEventSource*sender;};
struct LeoGraphicsCall {uint64_t transaction;uint32_t surface,reason,mismatch;bool backend;};
struct LeoGraphicsEvent {};
static uint32_t leo_graphics_state_last_timer_status;
struct {uint32_t last_timer_status;} leo_graphics_state;
static bool leo_graphics_try(){return true;}static void leo_graphics_unlock(){}
static void leo_graphics_note(LeoGraphicsEvent,bool,bool){}
static void leo_graphics_port_entry(uint32_t,uint32_t,UInt32,bool){}
static void leo_graphics_port_preflight(int){}static void leo_graphics_port_gate_entry(){}static void leo_graphics_port_return(int){}
static void leo_graphics_send_attempt(){}static void leo_graphics_send_result(uint32_t){}
static void IOLog(const char*,...){}
static uint64_t mach_absolute_time(){return 1000000000ULL;}
static void clock_interval_to_absolutetime_interval(unsigned value,unsigned,AbsoluteTime*out){*out=value;}
#define NSEC_PER_USEC 1000
#define __OSAbsoluteTime(v) (v)
static bool portCopyFail;static unsigned copied,released;static int sendResult,hwResult;static unsigned hardwareCalls;
static std::function<void()> duringSend;
static mach_port_t ipc_port_copy_send(mach_port_t port){assert(gateDepth && !lockDepth);if(portCopyFail)return 0;++copied;return port;}
static void setAsyncReference64(OSAsyncReference64 ref,mach_port_t port,io_user_reference_t cb,io_user_reference_t cookie){ref[0]=port;ref[1]=cb;ref[2]=cookie;}
static void releaseNotificationPort(mach_port_t){assert(!lockDepth);++released;}
static int ios7leo_display_power(uint32_t state){assert(gateDepth && !lockDepth && state<=2);++hardwareCalls;return hwResult;}
struct IOUserClient:Object {void free(){released=true;}};
class IOS7LabFramebufferClient:public IOUserClient {
public:
 ModelLock*transactionLock=new ModelLock;
 IOWorkLoop*vsyncWorkLoop=nullptr;IOTimerEventSource*vsyncTimer=new IOTimerEventSource;
 mach_port_t vsyncPort=0;io_user_reference_t vsyncCallback=0,vsyncRefcon=0;
 bool vsyncEnabled=false,vsyncRequested=false,vsyncClosed=false,vsyncPaused=false,vsyncFaulted=false;
 uint64_t vsyncCount=0,vsyncGeneration=1;uint32_t graphicsClient=1,graphicsOwnerPID=8;
 IOS7LabFramebufferClient*displayPowerNext=nullptr;bool displayPowerJoined=false;
 void terminate(){}
 void recordDisplayPower(uint32_t,int){}
 LeoGraphicsEvent graphicsEvent(uint32_t,uint32_t,int,const LeoGraphicsCall&){assert(lockDepth);return {};}
 void noteGraphics(uint32_t,uint32_t,int,const LeoGraphicsCall&){}
 int sendAsyncResult64Nonblocking(OSAsyncReference64,int,io_user_reference_t*,unsigned n){assert(gateDepth && !lockDepth && n==6);if(duringSend){auto fn=duringSend;duringSend={};fn();}return sendResult;}
 IOReturn applyVsyncTimer(bool,uint64_t);
 static IOReturn vsyncWorkAction(OSObject*,void*,void*,void*,void*);
 IOReturn displayPowerMembershipGated(bool);
 void observeVsyncPowerGated(uint32_t,int,uint32_t);
 IOReturn setDisplayPowerGated(uint32_t);
 void stopVsync();void stopVsyncGated();void emitVsync(IOTimerEventSource*);void emitVsyncGated(IOTimerEventSource*);
 IOReturn registerNotificationPort(mach_port_t,UInt32,io_user_reference_t);
 IOReturn registerNotificationPortGated(mach_port_t,UInt32,io_user_reference_t);
 IOReturn setVsyncCallbackGated(io_user_reference_t,io_user_reference_t);
 void free();IOReturn clientClose();
};
static IOWorkLoop *i9100_display_vsync_workloop;
static IOS7LabFramebufferClient *i9100_display_power_clients;
static bool i9100_display_vsync_paused;
volatile uint32_t i9100_display_vsync_power_diag[16]={1};
'''+ '\n\n'.join(methods)+r'''
static IOReturn action(IOS7LabFramebufferClient&c,unsigned operation,unsigned state=0){
 LeoVsyncAction a={operation,0,state,0,0,nullptr};return c.vsyncWorkLoop->runAction(IOS7LabFramebufferClient::vsyncWorkAction,&c,&a);
}
static void join(IOS7LabFramebufferClient&c,IOWorkLoop&loop){c.vsyncWorkLoop=&loop;loop.retain();c.vsyncTimer->retain();assert(action(c,LEO_VSYNC_JOIN)==0);}
static void subscribe(IOS7LabFramebufferClient&c,unsigned port,unsigned callback){
 assert(c.registerNotificationPort(port,0,0)==0);LeoVsyncAction a={LEO_VSYNC_CALLBACK,0,0,33,callback,nullptr};
 assert(c.vsyncWorkLoop->runAction(IOS7LabFramebufferClient::vsyncWorkAction,&c,&a)==0);
}
int main(){
 IOWorkLoop loop;i9100_display_vsync_workloop=&loop;
 IOS7LabFramebufferClient a,b,c;join(a,loop);join(b,loop);subscribe(a,10,20);subscribe(b,30,40);
 assert(a.vsyncTimer->armed && b.vsyncTimer->armed);a.emitVsync(a.vsyncTimer);assert(a.vsyncCount==1);
 assert(action(a,LEO_VSYNC_POWER,0)==0 && a.vsyncPaused && b.vsyncPaused && !a.vsyncTimer->armed && !b.vsyncTimer->armed);
 auto ports=std::make_pair(a.vsyncPort,b.vsyncPort);auto callbacks=std::make_pair(a.vsyncCallback,b.vsyncCallback);
 a.emitVsync(a.vsyncTimer);b.emitVsync(b.vsyncTimer);assert(a.vsyncCount==1 && b.vsyncCount==0);
 join(c,loop);subscribe(c,50,60);assert(c.vsyncPaused && !c.vsyncTimer->armed);
 subscribe(b,31,41);assert(!b.vsyncTimer->armed && b.vsyncPort==31 && b.vsyncCallback==41);
 assert(action(a,LEO_VSYNC_POWER,1)==0 && a.vsyncTimer->armed && b.vsyncTimer->armed && c.vsyncTimer->armed);
 assert(a.vsyncPort==ports.first && a.vsyncCallback==callbacks.first);
 hwResult=4;auto generation=a.vsyncGeneration;assert(action(a,LEO_VSYNC_POWER,0)==kIOReturnNotReady);
 assert(!a.vsyncPaused && a.vsyncGeneration==generation && a.vsyncTimer->armed);hwResult=0;
 portCopyFail=true;a.emitVsync(a.vsyncTimer);portCopyFail=false;assert(a.vsyncFaulted && !a.vsyncEnabled && !a.vsyncTimer->armed);
 assert(action(b,LEO_VSYNC_POWER,0)==0 && action(b,LEO_VSYNC_POWER,1)==0 && !a.vsyncTimer->armed && a.vsyncFaulted);
 subscribe(a,11,21);assert(a.vsyncTimer->armed && !a.vsyncFaulted);
 sendResult=77;a.emitVsync(a.vsyncTimer);assert(a.vsyncFaulted && !a.vsyncTimer->armed);
 sendResult=0;assert(action(b,LEO_VSYNC_POWER,1)==0 && !a.vsyncTimer->armed);
 subscribe(a,12,22);sendResult=MACH_SEND_TIMED_OUT;a.emitVsync(a.vsyncTimer);
 assert(a.vsyncTimer->armed && !a.vsyncFaulted);sendResult=0;
 duringSend=[&](){assert(action(b,LEO_VSYNC_POWER,0)==0);};a.emitVsync(a.vsyncTimer);
 assert(a.vsyncPaused && !a.vsyncTimer->armed && !b.vsyncTimer->armed);
 assert(action(b,LEO_VSYNC_POWER,2)==0 && a.vsyncTimer->armed);
 c.vsyncTimer->fail=true;assert(action(a,LEO_VSYNC_POWER,1)==kIOReturnNotReady && c.vsyncFaulted && !c.vsyncEnabled);
 c.vsyncTimer->fail=false;assert(action(a,LEO_VSYNC_POWER,1)==0 && !c.vsyncTimer->armed);subscribe(c,51,61);
 unsigned calls=hardwareCalls;c.clientClose();assert(action(c,LEO_VSYNC_POWER,1)==kIOReturnNotOpen && hardwareCalls==calls);
 assert(action(a,LEO_VSYNC_POWER,0)==0 && action(a,LEO_VSYNC_POWER,1)==0 && !c.vsyncEnabled);
 // Concurrent updates and power actions are serialized by the actual methods'
 // shared recursive gate; timer/IPC facade asserts no transaction lock held.
 std::thread config([&](){for(unsigned n=0;n<40;n++)subscribe(a,12,22);});
 std::thread power([&](){for(unsigned n=0;n<40;n++){assert(action(b,LEO_VSYNC_POWER,0)==0);assert(action(b,LEO_VSYNC_POWER,1)==0);}});
 config.join();power.join();assert(a.vsyncTimer->armed && b.vsyncTimer->armed);
 c.free();assert(!c.displayPowerJoined && c.released);assert(action(a,LEO_VSYNC_POWER,0)==0);
 a.free();b.free();assert(!i9100_display_power_clients);
 assert(i9100_display_vsync_power_diag[0]==1 && !(i9100_display_vsync_power_diag[1]&1));
 FILE*file=fopen("DOMAIN_FIXTURE","wb");assert(file && fwrite((const void*)i9100_display_vsync_power_diag,1,64,file)==64);fclose(file);
 assert(copied<=released && !lockDepth && !gateDepth);
 puts("PASS actual shared-gate display client methods: all-client pause/resume, preserved subscriptions, join asleep, close/free, fatal delivery and timer errors, temporary sends, reentrant generation and concurrent power/config");
}
'''
code=code.replace('DOMAIN_FIXTURE',str(out/'domain.bin'))
(out/'model.cpp').write_text(code)
for n,cmd in enumerate((['clang++','-std=c++11','-O1','-g','-fno-pie','-no-pie','-pthread','-fsanitize=address,undefined',
    '-fno-sanitize-recover=all',str(out/'model.cpp'),'-o',str(out/'model')],[str(out/'model')])):
    result=subprocess.run(cmd,text=True,capture_output=True);(out/f'command-{n}.log').write_text(result.stdout+result.stderr)
    if result.returncode:print((result.stdout+result.stderr)[-4000:],file=sys.stderr);result.check_returncode()
    if result.stdout:print(result.stdout.strip(),flush=True)
assert hashlib.sha256(source.read_bytes()).hexdigest()==before
import importlib.util,struct
spec=importlib.util.spec_from_file_location('display_power_host',repo.parent/'uniloader-S2I9100/scripts/i9100/inspect-charge-health.py')
host=importlib.util.module_from_spec(spec);spec.loader.exec_module(host)
raw=(out/'domain.bin').read_bytes();decoded=host.decode_vsync_power(raw,raw[:8])
assert decoded['consistent'] and decoded['domain_paused']==1 and decoded['requests']>0
version,sequence=struct.unpack_from('<2I',raw)
assert not host.decode_vsync_power(raw,struct.pack('<2I',version,sequence+2))['consistent']
(out/'domain.json').write_text(json.dumps(decoded,indent=2)+'\n')
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,source_sha256=before,methods=names,
    scope='Extracted actual production methods, IOKit/IPC/timer/FIMD boundary models; no deployed kernel or measured energy'),indent=2)+'\n')
print('EVIDENCE',out,flush=True)
