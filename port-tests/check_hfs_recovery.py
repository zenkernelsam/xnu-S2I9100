#!/usr/bin/env python3
"""Run the actual HFS-file I/O and COW error policy with storage callbacks."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import hashlib,json,subprocess,re
repo=Path(__file__).resolve().parents[1];platform=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
text=(platform/'IOS7LeoHFSFile.cpp').read_text()
def function(start):
    a=text.index(start);b=text.index('{',a);e=b+1;depth=1
    while depth:depth+=(text[e]=='{')-(text[e]=='}');e+=1
    return text[a:e]
methods='\n'.join(function(s) for s in ('static IOReturn cowResult(','IOReturn IOS7LeoHFSFile::doAsyncReadWrite('))
out=repo/'out'/('hfs-recovery-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "leo_cow.h"
#include "leo_read_cache.h"
using UInt64=uint64_t;using UInt8=uint8_t;using IOByteCount=uint64_t;using IOReturn=int;using IODirection=unsigned;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=-1,kIOReturnNotReady=-2,kIOReturnUnsupported=-3,kIOReturnUnderrun=-4,kIOReturnNoSpace=-5,kIODirectionIn=1,kIODirectionOut=2,StorageFailed=8};
struct IOStorageAttributes{unsigned options=0,reserved0032=0;uint64_t reserved0064=0;};
struct IOStorageCompletion{void(*action)(void*,IOReturn,UInt64);void*target;};
struct IOMemoryDescriptor{
 uint8_t bytes[512]={};unsigned direction=kIODirectionIn;
 UInt64 getLength(){return 512;}unsigned getDirection(){return direction;}
 UInt64 writeBytes(UInt64 at,const void*in,UInt64 n){assert(at+n<=512);memcpy(bytes+at,in,n);return n;}
 UInt64 readBytes(UInt64 at,void*out,UInt64 n){assert(at+n<=512);memcpy(out,bytes+at,n);return n;}
};
struct IOStorage{static void complete(IOStorageCompletion*c,IOReturn e,UInt64 n){c->action(c->target,e,n);}};
static unsigned locks;static void IOLockLock(void*){assert(!locks);locks++;}
static void IOLockUnlock(void*){assert(locks==1);locks--;}
static void ios7leo_root_file_status(){}static void IOLog(const char*,...){}
static unsigned failedPhase;static void phase(unsigned s,int,IOReturn){assert(s==StorageFailed);failedPhase++;}
static struct{unsigned cow_writes;}rootStatus;
struct IOS7LeoHFSFile{
 bool initialized=true,cowReady=true,faulted=false,present=true,failRead=false;
 unsigned reads=0;void*mutex=this;struct {uint64_t image_sectors=64;}image;
 leo_cow cow={};leo_read_cache*readCache=nullptr;
 uint8_t arena[4096*4];leo_cow_entry entries[8];uint32_t pending[4];
 static int base(void*c,uint64_t sector,uint8_t out[512]){
  auto f=(IOS7LeoHFSFile*)c;f->reads++;if(f->failRead)return -20;
  for(unsigned n=0;n<512;n++)out[n]=(uint8_t)(sector+n);return 0;
 }
 static int readCacheMedia(void*c){return ((IOS7LeoHFSFile*)c)->present?0:-21;}
 bool readCacheStatus(){return false;}void cowStatus(IOReturn,bool wrote,bool){if(wrote)rootStatus.cow_writes++;}
 IOReturn doAsyncReadWrite(IOMemoryDescriptor*,UInt64,UInt64,IOStorageAttributes*,IOStorageCompletion*);
 IOS7LeoHFSFile(){assert(leo_cow_init(&cow,64,4,8,arena,entries,pending,this,base)==0);}
};
''' + methods + r'''
struct Done{unsigned calls=0;IOReturn error=0;UInt64 bytes=0;static void receive(void*c,IOReturn e,UInt64 n){auto d=(Done*)c;assert(!locks);d->calls++;d->error=e;d->bytes=n;}};
int main(){
 alarm(10);IOStorageAttributes a;IOMemoryDescriptor buffer;Done d;IOStorageCompletion done={Done::receive,&d};
 for(unsigned missing=0;missing<2;missing++){
  IOS7LeoHFSFile f;f.failRead=true;f.present=!missing;failedPhase=0;memset(buffer.bytes,0xa5,512);d={};
  assert(f.doAsyncReadWrite(&buffer,3,1,&a,&done)==0 && d.calls==1 && d.error==-20 && !d.bytes);
  assert(f.faulted==bool(missing));for(unsigned n=0;n<512;n++)assert(buffer.bytes[n]==0xa5);
  f.failRead=false;unsigned before=f.reads;d={};
  auto result=f.doAsyncReadWrite(&buffer,3,1,&a,&done);
  if(missing){assert(result==kIOReturnNotReady && !d.calls && f.reads==before);}
  else{assert(result==0 && d.calls==1 && d.error==0 && d.bytes==512 && buffer.bytes[0]==3);}
 }
 IOS7LeoHFSFile f;buffer.direction=kIODirectionOut;memset(buffer.bytes,0x5a,512);
 f.failRead=true;d={};assert(f.doAsyncReadWrite(&buffer,8,1,&a,&done)==0 && d.error==-20);
 assert(!f.faulted && !f.cow.used && !f.cow.dirty); /* Failed fills never published. */
 f.failRead=false;d={};assert(f.doAsyncReadWrite(&buffer,8,1,&a,&done)==0 && !d.error && f.cow.used==1 && f.cow.dirty==1);
 buffer.direction=kIODirectionIn;f.failRead=true;d={};
 assert(f.doAsyncReadWrite(&buffer,8,1,&a,&done)==0 && !d.error && buffer.bytes[0]==0x5a); /* Existing COW data stays intact. */
 puts("PASS actual HFS I/O: one failed request stays an error with zero delivered data; present original media can serve the next request; absent media stays faulted; partial COW fills stay unpublished and existing writes persist");
}
'''
(out/'check.cpp').write_text(code)
common=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fno-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(platform)]
for n,args in enumerate((['clang','-std=c99',*common,'-c',str(platform/'leo_cow.c'),'-o',str(out/'cow.o')],['clang++','-std=c++11',*common,'-no-pie',str(out/'check.cpp'),str(out/'cow.o'),'-o',str(out/'check')],[str(out/'check')])):
    r=subprocess.run(args,text=True,capture_output=True);(out/f'command-{n}.log').write_text(r.stdout+r.stderr)
    if r.returncode:print((r.stdout+r.stderr)[-3000:],file=sys.stderr);r.check_returncode()
    if r.stdout:print(r.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,source_sha256={str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (platform/'IOS7LeoHFSFile.cpp',platform/'leo_cow.c',platform/'leo_cow.h')},scope='Actual extracted I/O function and production COW; IOStorage/medium/lock callbacks modeled'),indent=2)+'\n')
print('EVIDENCE',out)

