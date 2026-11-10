#!/usr/bin/env python3
"""Exercise actual AMFI output delivery with the unchanged entitlement blob."""
from datetime import datetime
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys


def main():
    if os.environ.get('I9100_CPU_CAPPED') != '1':
        os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                   sys.executable, __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
    repo = Path(__file__).resolve().parents[1]
    source_dir = repo / 'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
    out = repo / 'out' / ('amfi-output-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    historical = subprocess.check_output(['git', 'show',
        'd77ec79:iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7LeoDisplay.cpp'], cwd=repo, text=True)
    start = historical.index('externalMethod(', historical.index('class I9AmfiClient'))
    begin = historical.index('{', start)
    end = begin + 1
    depth = 1
    while depth:
        depth += (historical[end] == '{') - (historical[end] == '}')
        end += 1
    old = 'static IOReturn historical(uint32_t selector, IOExternalMethodArguments *args) ' + historical[begin:end]
    code = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
typedef int IOReturn;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,
      kIOReturnNoSpace=3,kIOReturnIOError=4};
#define IOLog(...) ((void)0)
typedef uint8_t uint8;
struct OSObject {virtual ~OSObject(){}; void release(){delete this;}};
struct OSData : OSObject {
 std::vector<unsigned char> bytes;
 static bool fail;
 static OSData *withCapacity(size_t n){if(fail)return 0;OSData *d=new OSData;d->bytes.reserve(n);return d;}
 bool appendBytes(const void *p,size_t n){const unsigned char *b=(const unsigned char*)p;bytes.insert(bytes.end(),b,b+n);return true;}
};
bool OSData::fail=false;
struct IOMemoryDescriptor {
 std::vector<unsigned char> bytes;bool fail;
 IOMemoryDescriptor(size_t n):bytes(n,0x5a),fail(false){}
 size_t getLength() const{return bytes.size();}
 size_t writeBytes(size_t at,const void *p,size_t n){if(fail)return 0;if(at+n>bytes.size())return 0;memcpy(bytes.data()+at,p,n);return n;}
};
struct IOExternalMethodArguments {
 void *structureOutput;size_t structureOutputSize;
 IOMemoryDescriptor *structureOutputDescriptor;size_t structureOutputDescriptorSize;
 OSObject **structureVariableOutputData;
 unsigned scalarInputCount,scalarOutputCount,structureInputSize;
 const void *structureInput;
 const uint64_t *scalarInput;
 IOMemoryDescriptor *structureInputDescriptor;
};
#include "i9_ent_blob.h"
#include "I9AmfiOutput.h"
#define CHECK(x) do {if(!(x)){printf("FAIL line %u: %s\n",__LINE__,#x);return 1;}checks++;}while(0)
''' + old + r'''
int main(int argc,char **argv){
 unsigned checks=0,copied=99;const size_t length=sizeof(i9_ent_blob)-8;
 const unsigned char *payload=i9_ent_blob+8;unsigned char token[32]={0};
 unsigned char buffer[4096+16];memset(buffer,0x5a,sizeof(buffer));
 IOExternalMethodArguments args={};args.structureInput=token;args.structureInputSize=32;
 uint64_t ownpid=67,foreignpid=42,highpid=0x100000043ULL;
 IOExternalMethodArguments self={};self.scalarInput=&ownpid;self.scalarInputCount=1;
 CHECK(i9_amfi_self_query(&self,67));
 self.scalarInput=&foreignpid;CHECK(!i9_amfi_self_query(&self,67));
 self.scalarInput=&highpid;CHECK(!i9_amfi_self_query(&self,67));
 self.scalarInput=&ownpid;self.scalarInputCount=2;CHECK(!i9_amfi_self_query(&self,67));
 self.scalarInputCount=1;self.structureInputSize=32;CHECK(!i9_amfi_self_query(&self,67));
 self.structureInputSize=0;CHECK(!i9_amfi_self_query(0,67));CHECK(!i9_amfi_self_query(&self,0));
 args.structureOutput=buffer;args.structureOutputSize=4096;
 CHECK(i9_amfi_output(&args,payload,length,&copied)==0);
 CHECK(copied==length+1 && args.structureOutputSize==length+1);
 CHECK(!memcmp(buffer,payload,length) && buffer[length]==0);
 for(size_t i=length+1;i<sizeof(buffer);i++)CHECK(buffer[i]==0x5a);
 if(argc==2){FILE *f=fopen(argv[1],"wb");CHECK(f);CHECK(fwrite(buffer,1,copied,f)==copied);fclose(f);}
 /* The historical method silently succeeds with no descriptor data. */
 IOMemoryDescriptor descriptor(4096);args={};args.structureInput=token;args.structureInputSize=32;
 args.structureOutputDescriptor=&descriptor;args.structureOutputDescriptorSize=4096;
 CHECK(historical(1,&args)==0);for(size_t i=0;i<descriptor.bytes.size();i++)CHECK(descriptor.bytes[i]==0x5a);
 CHECK(i9_amfi_output(&args,payload,length,&copied)==0);
 CHECK(args.structureOutputDescriptorSize==length+1 && copied==length+1);
 CHECK(!memcmp(descriptor.bytes.data(),payload,length) && descriptor.bytes[length]==0);
 descriptor=IOMemoryDescriptor(4096);args.structureOutputDescriptorSize=length;
 CHECK(i9_amfi_output(&args,payload,length,&copied)==kIOReturnNoSpace && copied==0);
 for(size_t i=0;i<descriptor.bytes.size();i++)CHECK(descriptor.bytes[i]==0x5a);
 args.structureOutputDescriptorSize=4096;descriptor.fail=true;
 CHECK(i9_amfi_output(&args,payload,length,&copied)==kIOReturnIOError && copied==0);
 OSObject *object=0;args={};args.structureVariableOutputData=&object;
 CHECK(historical(1,&args)==0 && !object);
 CHECK(i9_amfi_output(&args,payload,length,&copied)==0 && object && copied==length+1);
 OSData *data=static_cast<OSData*>(object);
 CHECK(data->bytes.size()==length+1 && !memcmp(data->bytes.data(),payload,length) && !data->bytes[length]);
 object->release();object=0;OSData::fail=true;
 CHECK(i9_amfi_output(&args,payload,length,&copied)==kIOReturnNoMemory && !object && copied==0);
 OSData::fail=false;args={};args.structureOutput=buffer;args.structureOutputSize=length;
 CHECK(i9_amfi_output(&args,payload,length,&copied)==kIOReturnNoSpace && copied==0);
 CHECK(i9_amfi_output(0,payload,length,&copied)==kIOReturnBadArgument && copied==0);
 args={};CHECK(i9_amfi_output(&args,payload,length,&copied)==kIOReturnBadArgument);
 printf("PASS %u AMFI descriptor/variable/inband delivery and error checks\n",checks);
 return 0;
}
'''
    path = out / 'test.cpp'
    path.write_text(code)
    binary = out / 'test'
    result = subprocess.run(['g++', '-std=c++11', '-O1', '-g', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-I' + str(source_dir), str(path), '-o', str(binary)],
        text=True, capture_output=True)
    (out / 'build.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    result = subprocess.run([str(binary), str(out / 'delivered.bin')], text=True, capture_output=True)
    (out / 'test.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    blob = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', (source_dir / 'i9_ent_blob.h').read_text()))
    delivered = (out / 'delivered.bin').read_bytes()
    assert delivered == blob[8:] + b'\0'
    assert plistlib.loads(delivered[:-1]) == plistlib.loads(blob[8:])
    print(result.stdout.strip())
    print('PASS unchanged entitlement dictionary; artifacts:', out)


if __name__ == '__main__':
    main()
