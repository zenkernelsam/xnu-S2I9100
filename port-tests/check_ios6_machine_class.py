#!/usr/bin/env python3
"""Check production machine-name scope against the original iOS6 classifier.

The AppleARMPE method executes with OSData/boot args modeled. The original
MobileGestalt class initializer and DeviceClass handler execute; libc/once
imports and sysctl input are modeled. No full OS or physical UI claim.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
                         sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
import hashlib,json,mmap,struct,subprocess
from pathlib import Path
from datetime import datetime
repo=Path(__file__).resolve().parents[1];root=repo.parents[1]
out=repo/'out'/('ios6-machine-class-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
text=(repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/AppleARMPE.cpp').read_text()
start=text.index('bool AppleARMPE::getMachineName(');brace=text.index('{',start);end=brace+1;depth=1
while depth:
    depth+=(text[end]=='{')-(text[end]=='}');end+=1
production=text[start:end]
source=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define BOARD_CONFIG_EXYNOS4210_I9100 1
static int abi=6;static const char*gIODTModelKey="model";
struct OSData {const char*getBytesNoCopy(){return "Samsung Galaxy S2 (GT-I9100)";}};
struct Provider {OSData data;void*getProperty(const char*){return &data;}};
struct AppleARMPE {Provider provider;Provider*getProvider(){return &provider;}bool getMachineName(char*,int);};
static void PE_parse_boot_argn(const char*key,int*out,unsigned n){assert(!strcmp(key,"i9100hidabi")&&n==sizeof(int));*out=abi;}
static size_t strlcpy(char*dst,const char*src,size_t size){size_t n=strlen(src);if(size){unsigned take=n<size-1?n:size-1;memcpy(dst,src,take);dst[take]=0;}return n;}
'''+production+r'''
int main(){AppleARMPE platform;char name[64];assert(platform.getMachineName(name,sizeof(name)));assert(!strcmp(name,"iPhone3,1"));
 FILE*f=fopen("compat-name.txt","wb");assert(f);fwrite(name,1,strlen(name),f);fclose(f);
 assert(!platform.getMachineName(0,64));assert(!platform.getMachineName(name,0));
 char guard[4]={1,2,3,4};assert(platform.getMachineName(guard+1,1));assert(guard[0]==1&&guard[1]==0&&guard[2]==3&&guard[3]==4);
 abi=7;assert(platform.getMachineName(name,64));assert(!strcmp(name,"Samsung Galaxy S2 (GT-I9100)"));
 abi=0;assert(platform.getMachineName(name,64));assert(!strcmp(name,"Samsung Galaxy S2 (GT-I9100)"));
 puts("PASS production S2/iOS6 machine name, buffer boundaries, other ABI native model");}
'''
(out/'check.cpp').write_text(source)
subprocess.run(['c++','-O1','-fsanitize=address,undefined',str(out/'check.cpp'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],cwd=out,check=True)
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE,UC_HOOK_MEM_UNMAPPED
from unicorn.arm_const import *
STOP=0x1001fff0;HEAP=0x10000000
with (root/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as file:
    cache=mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ)
    mo,mc=struct.unpack_from('<II',cache,16);maps=[struct.unpack_from('<QQQII',cache,mo+n*32) for n in range(mc)]
    def read(va,n):
        st,sz,fo,_,_=next(x for x in maps if x[0]<=va and va+n<=x[0]+x[1]);return cache[fo+va-st:fo+va-st+n]
    def execute(machine):
        cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB);cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);cpu.mem_map(HEAP,0x20000);pages=set()
        def page(uc,access,at,n,value,unused):
            p=at&~4095
            if p in pages:return True
            uc.mem_map(p,4096);uc.mem_write(p,read(p,4096));pages.add(p);return True
        cpu.hook_add(UC_HOOK_MEM_UNMAPPED,page)
        for at in (0x3acd7bd0,0x3acd5300):page(cpu,0,at,4,0,None)
        cpu.mem_write(HEAP,machine.encode()+b'\0');cpu.mem_write(0x3acd7be4,struct.pack('<I',0xffffffff))
        cpu.mem_write(0x3acd7be8,struct.pack('<I',HEAP));cpu.mem_write(0x3acd7bec,struct.pack('<I',0xffffffff))
        cpu.mem_write(0x3acd5300,bytes(4))
        def ret(value):
            lr=cpu.reg_read(UC_ARM_REG_LR);psr=cpu.reg_read(UC_ARM_REG_CPSR)
            cpu.reg_write(UC_ARM_REG_R0,value&0xffffffff);cpu.reg_write(UC_ARM_REG_CPSR,(psr&~32)|(32 if lr&1 else 0));cpu.reg_write(UC_ARM_REG_PC,lr)
        def hook(uc,at,size,unused):
            if at==0x38681ab4:
                a,b,n=[uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
                if b&~4095 not in pages:page(uc,0,b,n,0,None)
                left=bytes(uc.mem_read(a,n)).split(b'\0',1)[0];right=bytes(uc.mem_read(b,n)).split(b'\0',1)[0]
                ret(0 if left==right else -1 if left<right else 1)
            elif at==STOP:uc.emu_stop()
        cpu.hook_add(UC_HOOK_CODE,hook)
        def call(at):
            cpu.reg_write(UC_ARM_REG_SP,HEAP+0x1f000);cpu.reg_write(UC_ARM_REG_LR,STOP|1)
            cpu.emu_start(at|1,STOP,count=10000)
            assert cpu.reg_read(UC_ARM_REG_PC)==STOP
            return cpu.reg_read(UC_ARM_REG_R0)
        call(0x3867a5e8);category=struct.unpack('<I',cpu.mem_read(0x3acd5300,4))[0]
        result=call(0x3867b57c);answer=None
        if result:
            if result&~4095 not in pages:page(cpu,0,result,16,0,None)
            cf=bytes(cpu.mem_read(result,16));ptr,length=struct.unpack_from('<II',cf,8)
            if ptr&~4095 not in pages:page(cpu,0,ptr,length,0,None)
            answer=bytes(cpu.mem_read(ptr,length)).decode('ascii')
        return dict(machine=machine,native_category=category,DeviceClass=answer,answer_pointer=hex(result))
    old=execute('Samsung Galaxy S2 (GT-I9100)');new=execute((out/'compat-name.txt').read_text())
    assert old['DeviceClass'] is None and new['DeviceClass']=='iPhone' and new['native_category']==1
    report=dict(passed=True,old=old,new=new,scope=__doc__,phone_operations=0)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print('EVIDENCE',out)
