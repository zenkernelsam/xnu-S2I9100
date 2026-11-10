#!/usr/bin/env python3
"""Production async sender lifetime/ARM32 wire and retained abort-buffer checks.

IPC allocation/queue operations are modeled at their declared boundary. The
actual sender and abort-buffer functions are compiled with ASan/UBSan; the
native headers and actual IPC implementation are checked by the ARM build.
"""
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                         sys.executable, __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
repo = Path(__file__).resolve().parents[1]
source = (repo/'iokit/Kernel/IOUserClient.cpp').read_text()
start = source.index('IOReturn IOUserClient::sendAsyncResult64(')
sender = source[start:source.index('\n/* * * *', start)]
ipc = (repo/'osfmk/kern/ipc_mig.c').read_text()
start = ipc.index('static mach_msg_return_t\nmach_msg_send_from_kernel_policy(')
transport = ipc[start:ipc.index('\n#if IKM_SUPPORT_LEGACY', start)]
out = repo/'out'/('display-notification-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True, exist_ok=False)
prefix = r'''
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
using UInt32=uint32_t; using IOReturn=int32_t; using kern_return_t=int32_t;
using mach_port_t=uint32_t; using io_user_reference_t=uint64_t;
using mach_msg_return_t=int32_t;using mach_msg_size_t=uint32_t;using mach_msg_option_t=uint32_t;
using OSAsyncReference64=uint64_t[8];
enum { MACH_PORT_NULL=0, kMaxAsyncArgs=16, kOSAsyncRefCount=8,
 kOSNotificationMessageID=53,kIOAsyncCompletionNotificationType=150,
 MACH_MSG_TYPE_COPY_SEND=19,MACH_SEND_TIMEOUT=0x10,MACH_SEND_ALWAYS=0x10000,
 MACH_MSG_SUCCESS=0,MACH_MSG_TIMEOUT_NONE=0,KERN_SUCCESS=0,kIOReturnSuccess=0,kIOReturnMessageTooLarge=0x7e,
 MACH_SEND_TIMED_OUT=0x10000004,MACH_SEND_NO_BUFFER=0x1000000d };
static const uint64_t kIOUCAsync0Flags=3,kIOUCAsync64Flag=1;
#define MACH_MSGH_BITS(remote,local) ((remote)|((local)<<8))
#define REF32(x) ((int)(x))
#define bzero(p,n) std::memset(p,0,n)
#define bcopy(a,b,n) std::memcpy(b,a,n)
#pragma pack(push,4)
struct mach_msg_header_t { uint32_t msgh_bits,msgh_size,msgh_remote_port,msgh_local_port,msgh_reserved;int32_t msgh_id; };
struct OSNotificationHeader { uint32_t size,type,reference[8]; };
struct OSNotificationHeader64 { uint32_t size,type;uint64_t reference[8]; };
struct IOAsyncCompletionContent { int32_t result; };
#pragma pack(pop)
static unsigned checks;
static void check(bool ok,const char *why) { ++checks; if(!ok) { std::fprintf(stderr,"FAIL %s\n",why);std::exit(1); } }
struct Message { std::vector<unsigned char> bytes;bool live=false,rights=false; };
using ipc_kmsg_t=Message*;
static Message message;
static std::vector<unsigned char> last;
static int getError,copyError,sendError;
static unsigned allocationCalls,copies,sends,frees,destroys,proper,queueCount,queueLimit=16,lastOptions;
static int ipc_kmsg_get_from_kernel(mach_msg_header_t *h,unsigned size,ipc_kmsg_t *out) {
 ++allocationCalls;check(!message.live,"no abandoned allocation");last.assign((unsigned char*)h,(unsigned char*)h+size);
 if(getError)return getError;message.bytes=last;message.live=true;message.rights=false;*out=&message;return 0;
}
static int ipc_kmsg_copyin_from_kernel(ipc_kmsg_t m) {
 ++copies;check(m->live,"copyin owns allocation");if(copyError)return copyError;m->rights=true;return 0;
}
static void ipc_kmsg_free(ipc_kmsg_t m) { ++frees;check(m->live&&!m->rights,"free only before rights consumed");m->live=false; }
static void ipc_kmsg_destroy(ipc_kmsg_t m) { ++destroys;check(m->live&&m->rights,"destroy releases copied rights");m->live=m->rights=false; }
static int ipc_kmsg_send(ipc_kmsg_t m,unsigned option,unsigned timeout) {
 ++sends;lastOptions=option;if(option==MACH_SEND_ALWAYS)++proper;check(m->live&&m->rights,"send owns copied rights");check(timeout==0,"no blocking deadline");
 if(sendError)return sendError;
 if(option==MACH_SEND_TIMEOUT && queueCount>=queueLimit)return MACH_SEND_TIMED_OUT;
 ++queueCount;m->live=m->rights=false;return 0;
}
static void IOLog(const char *,...) {}
class IOUserClient { public:
 static IOReturn sendAsyncResult64(OSAsyncReference64,IOReturn,io_user_reference_t[],UInt32);
 static IOReturn sendAsyncResult64Nonblocking(OSAsyncReference64,IOReturn,io_user_reference_t[],UInt32);
 static IOReturn sendAsyncResult64Internal(OSAsyncReference64,IOReturn,io_user_reference_t[],UInt32,bool);
};
'''
suffix = r'''
static uint32_t word(unsigned at) { uint32_t v;std::memcpy(&v,last.data()+at*4,4);return v; }
int main() {
 OSAsyncReference64 ref={0x12345000,0x32100331,0xabcdef};
 io_user_reference_t args[6]={0xfedcba98,0x12345,0x12345678,0x6789,16667000,0};
 check(IOUserClient::sendAsyncResult64Nonblocking(ref,0,args,6)==0,"nonblocking success");
 check(last.size()==92 && word(0)==19 && word(1)==92 && word(2)==0x12345000 && word(3)==0 && word(4)==0 && word(5)==53,"real captured ARM32 header");
 check(word(6)==28 && word(7)==150 && word(9)==0x32100331 && word(10)==0xabcdef && word(16)==0,"notification/refcon/result layout");
 check(word(17)==0xfedcba98 && word(22)==0 && lastOptions==MACH_SEND_TIMEOUT && proper==0,"argument bits and ordinary queue limit");
 queueCount=0;unsigned dropped=0;
 for(unsigned i=0;i<1000;i++){int r=IOUserClient::sendAsyncResult64Nonblocking(ref,0,args,6);if(r==MACH_SEND_TIMED_OUT)++dropped;else check(r==0,"only real delivery or skipped tick");}
 check(queueCount==16 && dropped==984 && !message.live && !message.rights,"stalled receiver stays bounded with no leaked rights");
 check(IOUserClient::sendAsyncResult64(ref,0,args,6)==0 && queueCount==17 && lastOptions==MACH_SEND_ALWAYS && proper==1,"ordinary callers retain send-always policy");
 unsigned oldDestroy=destroys,oldFree=frees,oldCopy=copies;
 getError=MACH_SEND_NO_BUFFER;check(IOUserClient::sendAsyncResult64Nonblocking(ref,0,args,6)==MACH_SEND_NO_BUFFER && copies==oldCopy && frees==oldFree && destroys==oldDestroy,"allocation failure not freed twice");getError=0;
 copyError=0x1000000a;check(IOUserClient::sendAsyncResult64Nonblocking(ref,0,args,6)==copyError && frees==oldFree+1 && destroys==oldDestroy && !message.live,"copyin failure cleanup");copyError=0;
 sendError=MACH_SEND_NO_BUFFER;check(IOUserClient::sendAsyncResult64Nonblocking(ref,0,args,6)==MACH_SEND_NO_BUFFER && destroys==oldDestroy+1 && !message.live&&!message.rights,"send failure destroys message and rights");sendError=0;
 OSAsyncReference64 empty={0};unsigned oldGets=allocationCalls;
 check(IOUserClient::sendAsyncResult64Nonblocking(empty,0,args,6)==0 && allocationCalls==oldGets,"null port no message");
 check(IOUserClient::sendAsyncResult64Nonblocking(ref,0,args,17)==kIOReturnMessageTooLarge && allocationCalls==oldGets,"argument capacity before allocation");
 queueCount=0;ref[0]|=1;args[0]=UINT64_C(0x12345678fedcba98);
 check(IOUserClient::sendAsyncResult64Nonblocking(ref,0,args,6)==0 && last.size()==148,"64-bit async-reference wire shape");
 uint64_t value;std::memcpy(&value,last.data()+100,8);check(value==args[0],"64-bit arguments preserved");
 std::printf("%u production sender/lifetime/wire checks passed; IPC boundary modeled\n",checks);
}
'''
abort = r'''
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define I9100_ABORT_IMPLEMENTATION
#include "i9100_abort_observation.h"
static unsigned checks;
static void check(bool ok,const char *why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
 static_assert(sizeof(I9100AbortObservation)==336,"retained wire size");
 const char *one="uncaught excep",*two="tion: first SB reason\n";
 i9100_stderr_append(57,1,one,std::strlen(one));i9100_stderr_append(57,1,two,std::strlen(two));
 const char *bb="assertion failed: first BB reason\n";
 i9100_stderr_append_to(&i9100_backboard_stderr_buffer,20,2,bb,std::strlen(bb));
 I9100AbortTrace sb={57,1,0x1001,0x1003,0x2000,0x2020,2,0,{0x3001,0x4001},0,0,0,0,0};
 I9100AbortTrace back={20,2,0x5001,0x5003,0x6000,0x6020,1,0,{0x7001},0,0,0,0,0};
 i9100_abort_store(&sb);i9100_abort_store_to(&i9100_backboard_abort_observation,&i9100_backboard_stderr_buffer,&back);
 check(i9100_abort_observation.trace.pid==57 && i9100_backboard_abort_observation.trace.pid==20,"roles remain independent");
 check(std::strstr((const char*)i9100_abort_observation.text,"first SB reason") && std::strstr((const char*)i9100_backboard_abort_observation.text,"first BB reason"),"fragmented first diagnostic retained per role");
 const char *footer="malloc: later footer\n";i9100_stderr_append_to(&i9100_backboard_stderr_buffer,20,2,footer,std::strlen(footer));
 i9100_abort_store_to(&i9100_backboard_abort_observation,&i9100_backboard_stderr_buffer,&back);
 check(i9100_backboard_abort_observation.sequence==2 && std::strstr((const char*)i9100_backboard_abort_observation.text,"first BB reason"),"footer does not erase first failure");
 back.pid=21;back.task=3;i9100_abort_store_to(&i9100_backboard_abort_observation,&i9100_backboard_stderr_buffer,&back);
 check(!i9100_backboard_abort_observation.text_kind && !i9100_backboard_abort_observation.text[0],"foreign PID/task cannot borrow stderr");
 char longLine[129];std::memset(longLine,'x',128);longLine[128]=0;
 for(int i=0;i<8;i++)i9100_stderr_append_to(&i9100_backboard_stderr_buffer,21,3,longLine,128);
 i9100_abort_store_to(&i9100_backboard_abort_observation,&i9100_backboard_stderr_buffer,&back);
 check(i9100_backboard_stderr_buffer.used==512 && i9100_backboard_abort_observation.text[240]==0 && i9100_backboard_abort_observation.text_kind==2,"bounded ring and terminated fallback");
 char line[80];check(i9100_abort_line(0,57,6,line,sizeof(line)) && !i9100_abort_line(0,21,6,line,sizeof(line)),"backboard snapshot does not become an SB overlay");
 i9100_backboard_abort_observation.sequence=UINT32_MAX;
 i9100_abort_store_to(&i9100_backboard_abort_observation,&i9100_backboard_stderr_buffer,&back);
 check(i9100_backboard_abort_observation.sequence==1 && i9100_abort_observation.sequence==1,"sequence wrap remains published and isolated");
 std::printf("%u retained abort isolation/bounds checks passed\n",checks);
}
'''
for name, text in [('sender',prefix+transport+sender+suffix), ('abort',abort)]:
    cpp=out/(name+'.cpp');binary=out/name;cpp.write_text(text)
    subprocess.run(['clang++','-std=c++11','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all','-fno-omit-frame-pointer',
        '-I',str(repo/'pexpert/arm'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print(out,flush=True)
