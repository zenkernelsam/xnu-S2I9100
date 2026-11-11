#!/usr/bin/env python3
"""Check the actual injected queue event against CopyEvent's cached source.

Workloop scheduling, clock and queue storage are modeled; the production
pollInput body and both native wire serializers execute unchanged.
"""
import os
from pathlib import Path
import sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
import subprocess
from datetime import datetime
repo=Path(__file__).resolve().parents[1]
base=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
text=(base/'IOS7LeoHIDTouch.cpp').read_text()
start=text.index('void IOHIDEventService::pollInput(void)')
end=text.index('void IOHIDEventService::pollHome(void)',start)
body=text[start:end]
source=r'''
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <stdio.h>
'''+''.join(f'#include "{base/name}"\n' for name in ('IOS7LeoTouchWire.h','i9100_home.h','IOS6I9100HIDWire.h','I9100InputTest.h'))+r'''
#define OSMemoryBarrier() ((void)0)
#define IOLog(...) ((void)0)
typedef uint64_t AbsoluteTime;
static uint64_t clock_now=1000000000;
static uint64_t mach_absolute_time(){return clock_now;}
static void nanoseconds_to_absolutetime(uint64_t n,AbsoluteTime *out){*out=n;}
#define __OSAbsoluteTime(x) (x)
static void leo_touch_inc(unsigned *value){++*value;}
volatile uint32_t i9100_input_test[16];
struct Queue {
    IOS6TouchPacket packet;unsigned calls=0;bool reject=false;
    bool sane(){return true;}
    bool enqueue(const void *p,unsigned size){
        if(reject)return false;assert(size==sizeof(packet));memcpy(&packet,p,size);++calls;return true;
    }
};
struct IOHIDEventService {
    bool opened=true,pending=false,previousDown=false,lastValid=false;
    uint64_t inputDuration=0,inputStart=0;
    unsigned enqueued=0,downEvents=0,moveEvents=0,upEvents=0;
    LeoHIDPacket lastPacket={};Queue storage;Queue *queue=&storage;
    uint64_t getRegistryEntryID(){return 0x12345678;}
    void publishSnapshot(){}
    void pollInput();
};
'''+body+r'''
static void request(){
    memset((void*)i9100_input_test,0,sizeof(i9100_input_test));
    i9100_input_test[0]=1;i9100_input_test[1]=2;i9100_input_test[2]=1;
    i9100_input_test[3]=130;i9100_input_test[4]=750;
    i9100_input_test[5]=380;i9100_input_test[6]=750;i9100_input_test[7]=400;
}
static void same_event(IOHIDEventService &s){
    IOS6TouchPacket copied;
    assert(s.lastValid && ios6_touch_wire(&s.lastPacket,&copied));
    assert(!memcmp(&copied,&s.storage.packet,sizeof(copied)));
}
int main(){
    IOHIDEventService s;request();s.pollInput();same_event(s);
    assert(s.downEvents==1 && i9100_input_test[1]==3);
    clock_now+=200000000;s.pollInput();same_event(s);
    assert(s.moveEvents==1);
    clock_now+=200000000;s.pollInput();same_event(s);
    assert(s.upEvents==1 && !s.previousDown && i9100_input_test[1]==4);
    assert(s.storage.calls==3 && s.enqueued==3);
    LeoHIDPacket retained=s.lastPacket;
    request();s.storage.reject=true;s.pollInput();
    assert(s.lastValid && !memcmp(&retained,&s.lastPacket,sizeof(retained)) && s.enqueued==3);
    IOHIDEventService never;request();never.storage.reject=true;never.pollInput();
    assert(!never.lastValid && !never.enqueued);
    puts("PASS actual injection queue and CopyEvent cache match for down/move/up; failed enqueue preserves cache; scheduler/queue modeled");
}
'''
out=repo/'out'/('input-cache-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
(out/'check.cpp').write_text(source)
subprocess.run(['c++','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                str(out/'check.cpp'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True)
print('EVIDENCE',out)
