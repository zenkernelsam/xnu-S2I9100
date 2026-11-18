#!/usr/bin/env python3
"""Exercise the actual Power polling method and decode iOS6 key packets."""
import os,sys,subprocess,re
from pathlib import Path
from datetime import datetime
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1];header=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
source=(header/'IOS7LeoHIDTouch.cpp').read_text();begin=source.index('void IOHIDEventService::pollPower(');end=source.index('\nvoid IOHIDEventService::pollHome(',begin);body=source[begin:end]
out=repo/'out'/('power-key-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
prefix=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include "IOS7LeoTouchWire.h"
#include "i9100_home.h"
#include "IOS6I9100HIDWire.h"
#include "i9100_home_observation.h"
volatile I9100HomeObservation i9100_power_key_observation;
volatile uint32_t i9100_input_test[16];
using AbsoluteTime=uint64_t;
static void nanoseconds_to_absolutetime(uint64_t n,AbsoluteTime*out){*out=n;}
#define __OSAbsoluteTime(x) (x)
#define OSMemoryBarrier() __sync_synchronize()
struct Queue{bool blocked=false;std::vector<std::vector<unsigned char>>packets;bool sane(){return true;}
bool enqueue(const void*p,unsigned n){if(blocked)return false;packets.emplace_back((const unsigned char*)p,(const unsigned char*)p+n);return true;}
uint32_t readerHead(){return 0;}uint32_t writerTail(){return packets.size()*60;}};
class IOHIDEventService{public:Queue backing;Queue*queue=&backing;I9100HomeState powerState;bool opened=true,ios6Wire=true;uint32_t pins=128;uint64_t inputStart=0,inputDuration=0;
IOHIDEventService(){i9100_home_reset(&powerState);}static uint32_t readRegister(void*p,unsigned region,unsigned at){assert(region==1&&at==I9100_POWER_DAT);return ((IOHIDEventService*)p)->pins;}
uint64_t getRegistryEntryID(){return 0x100000001ULL;}void pollPower(uint64_t,uint64_t);};
static uint32_t word(const std::vector<unsigned char>&b,unsigned offset){uint32_t v;memcpy(&v,b.data()+offset,4);return v;}
'''
suffix=r'''
int main(){uint32_t all=0xdeadbeef;assert(i9100_power_key_restore_con(i9100_power_key_input_con(all),all)==all);assert(i9100_power_key_restore_pull(i9100_power_key_input_pull(all),all)==all);
IOHIDEventService s;s.pollPower(1000000000ULL,20000000);s.pins=0;s.pollPower(1010000000ULL,20000000);s.pollPower(1030000000ULL,20000000);s.pins=128;s.pollPower(1040000000ULL,20000000);s.pollPower(1060000000ULL,20000000);
assert(s.backing.packets.size()==2);for(unsigned n=0;n<2;n++){auto&p=s.backing.packets[n];assert(p.size()==56&&word(p,12)==1&&word(p,16)==40&&word(p,20)==3&&word(p,48)==0x0030000c&&word(p,52)==(n==0));assert(ios6_home_match(p.data(),p.size()));}
IOHIDEventService held;held.pins=0;held.pollPower(1000000000ULL,20000000);held.pollPower(1100000000ULL,20000000);assert(held.backing.packets.empty());held.pins=128;held.pollPower(1200000000ULL,20000000);held.pollPower(1230000000ULL,20000000);assert(held.backing.packets.empty());
IOHIDEventService test;memset((void*)i9100_input_test,0,sizeof(i9100_input_test));test.pollPower(1000000000ULL,20000000);i9100_input_test[0]=1;i9100_input_test[1]=2;i9100_input_test[7]=200;i9100_input_test[13]=2;
test.pollPower(1100000000ULL,20000000);test.pollPower(1130000000ULL,20000000);test.pollPower(1310000000ULL,20000000);test.pollPower(1340000000ULL,20000000);assert(i9100_input_test[1]==4&&i9100_input_test[8]==2&&test.backing.packets.size()==2);
IOHIDEventService full;memset((void*)i9100_input_test,0,sizeof(i9100_input_test));full.pollPower(1000000000ULL,20000000);full.backing.blocked=true;full.pins=0;full.pollPower(1010000000ULL,20000000);full.pollPower(1030000000ULL,20000000);full.pins=128;full.pollPower(1040000000ULL,20000000);full.pollPower(1060000000ULL,20000000);assert(full.powerState.count==2);full.backing.blocked=false;full.pollPower(1100000000ULL,20000000);assert(full.powerState.count==0&&full.backing.packets.size()==2);
puts("PASS GPIO fields preserved; real pollPower physical/held boot/synthetic/queue-full; native56B Consumer Power down/up");}
'''
(out/'check.cpp').write_text(prefix+body+suffix)
for command in (['clang++','-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(header),str(out/'check.cpp'),'-o',str(out/'check')],[str(out/'check')]):
    result=subprocess.run(command,text=True,capture_output=True);(out/('compile.log' if command[0]=='clang++' else 'result.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
print('EVIDENCE',out)
