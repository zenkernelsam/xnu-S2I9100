#!/usr/bin/env python3
"""Exercise the production exec workspace rule and original UIKit decision."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__,*sys.argv[1:]],dict(os.environ,I9100_CPU_CAPPED='1'))
import argparse,importlib.util,json,mmap,struct,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[1];root=repo.parent

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if not out.is_relative_to(repo/'out') or out.exists():p.error('Fresh XNU out directory required')
    (out/'include/libkern').mkdir(parents=True)
    (out/'include/libkern/OSAtomic.h').write_text('typedef uint32_t UInt32;\nstatic int OSCompareAndSwap(UInt32 a,UInt32 b,volatile UInt32 *p){return __sync_bool_compare_and_swap(p,a,b);}\n')
    source=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
struct component {char *cn_nameptr;int cn_namelen;};
struct nameidata {struct component ni_cnd;};
struct image_params {char *ip_strings,*ip_endargv,*ip_endenvv,*ip_strendp;int ip_envc,ip_argspace,ip_strspace;struct nameidata *ip_ndp;};
static int enabled=1,abi=6;
static int PE_parse_boot_argn(const char *key,void *dest,int size){assert(size==4);*(int *)dest=!strcmp(key,"i9100setupnative")?enabled:abi;return 1;}
#include "bsd/dev/arm/i9100_setup_native_layout.h"
static unsigned checks;
static void check(const char *input,size_t length,int count,const char *expected,int expected_changed,int mode){
 char workspace[256],before[256];memset(workspace,0xa5,sizeof(workspace));memcpy(workspace,input,length);memcpy(before,workspace,sizeof(workspace));
 struct nameidata nd={{"Setup",5}};
 struct image_params ip={workspace,workspace,workspace+length,workspace+length,count,999,888,&nd};
 uint8_t uuid[16]={0x58,0x0d,0xfd,0x1e,0xdb,0xd5,0x3d,0x33,0x84,0x4b,0xca,0x66,0x35,0xc0,0xb3,0xcd};
 unsigned pointer_size=4;enabled=1;abi=6;
 if(mode==1)enabled=0;if(mode==2)uuid[0]^=1;if(mode==3)nd.ni_cnd.cn_nameptr="Other";
 if(mode==4)pointer_size=8;if(mode==5)abi=7;if(mode==6)ip.ip_endenvv=workspace-0;
 memset((void *)i9100_setup_native_layout_diag,0,sizeof(i9100_setup_native_layout_diag));i9100_setup_native_layout_diag[0]=1;
 i9100_setup_native_layout(&ip,uuid,pointer_size);
 assert(ip.ip_argspace==999 && ip.ip_strspace==888 && ip.ip_envc==count);
 assert(!memcmp(workspace+length,before+length,sizeof(workspace)-length));
 assert(!memcmp(workspace,expected,length));assert(i9100_setup_native_layout_diag[3]==(unsigned)expected_changed);checks++;
}
#define CASE(input,count,expected,changed,mode) check(input,sizeof(input)-1,count,expected,changed,mode)
int main(void){
 CASE("HOME=/var/mobile\0CLASSIC=1\0PATH=/bin\0",3,"HOME=/var/mobile\0CLASSIC=0\0PATH=/bin\0",1,0);
 for(int n=1;n<=6;n++)CASE("CLASSIC=1\0",1,"CLASSIC=1\0",0,n);
 CASE("CLASSIC=0\0",1,"CLASSIC=0\0",0,0);
 CASE("CLASSIC=2\0",1,"CLASSIC=2\0",0,0);
 CASE("CLASSIC=10\0",1,"CLASSIC=10\0",0,0);
 CASE("CLASSIC=\0",1,"CLASSIC=\0",0,0);
 CASE("CLASSICALLY=1\0",1,"CLASSICALLY=1\0",0,0);
 CASE("HOME=/var/mobile\0",1,"HOME=/var/mobile\0",0,0);
 CASE("CLASSIC=1\0CLASSIC=0\0",2,"CLASSIC=1\0CLASSIC=0\0",0,0);
 CASE("CLASSIC=1\0BROKEN",2,"CLASSIC=1\0BROKEN",0,0);
 CASE("CLASSIC=1\0",2,"CLASSIC=1\0",0,0);
 CASE("CLASSIC=1\0",257,"CLASSIC=1\0",0,0);
 CASE("CLASSIC=1\0",-1,"CLASSIC=1\0",0,0);
 CASE("",0,"",0,0);
 enabled=1;abi=6;i9100_setup_native_layout_diag[3]=UINT32_MAX;i9100_setup_native_count(3);assert(i9100_setup_native_layout_diag[3]==UINT32_MAX);
 printf("PASS %u production exec workspace/identity/duplicate/malformed/default-off cases and saturated diagnostics\n",checks);
}
'''
    (out/'production.c').write_text(source)
    compile=subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined','-I'+str(out/'include'),'-I'+str(repo),out/'production.c','-o',out/'production'],text=True,capture_output=True,check=True)
    (out/'compiler.txt').write_text(compile.stdout+compile.stderr)
    test=subprocess.run([out/'production'],text=True,capture_output=True);(out/'production.txt').write_text(test.stdout+test.stderr)
    test.check_returncode()
    sys.path.insert(0,str(root/'assets/py-native-hid'))
    from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
    from unicorn.arm_const import UC_CPU_ARM_CORTEX_A9,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R6,UC_ARM_REG_SP,UC_ARM_REG_LR
    s=importlib.util.spec_from_file_location('native_startup',root/'uniloader-S2I9100/scripts/i9100/symbolize-ios6-stacks.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
    with (root/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as f:
        with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as d:
            c=m.Cache(d);code=d[c.offset(0x32f870e8,26):c.offset(0x32f870e8,26)+26]
            selector=struct.unpack_from('<I',d,c.offset(0x397d8ba0,4))[0]
            assert c.string(c.offset(selector),len(d))=='_setIsClassic:'
            decisions=[]
            for value,expected in ((1,1),(0,0),(2,0)):
                u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
                u.mem_map(0x32f87000,4096);u.mem_write(0x32f870e8,code)
                u.mem_map(0x397d8000,4096);u.mem_write(0x397d8ba0,struct.pack('<I',selector))
                u.mem_map(0x333a0000,4096);u.mem_write(0x333a0aa4,b'\x1e\xff\x2f\xe1')
                u.mem_map(0x10000000,4096);u.reg_write(UC_ARM_REG_SP,0x10000ff0)
                u.reg_write(UC_ARM_REG_R0,value);u.reg_write(UC_ARM_REG_R6,0x10000080)
                observed=[]
                def hit(uc,address,size,data):
                    if address==0x333a0aa4:
                        observed.append((uc.reg_read(UC_ARM_REG_R0),uc.reg_read(UC_ARM_REG_R1),uc.reg_read(UC_ARM_REG_R2)));uc.emu_stop()
                u.hook_add(UC_HOOK_CODE,hit);u.emu_start(0x32f870e9,0x32f87102,count=32)
                assert observed==[(0x10000080,selector,expected)],observed
                decisions.append(dict(original_int_value=value,original_setIsClassic_argument=expected))
    result=dict(result='PASS',production_output=test.stdout.strip(),original_cortex_a9_decisions=decisions,
        target_writes=False,limits='Original decision instructions execute; UIKit geometry/compositor and actual exec environment require the phone')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
if __name__=='__main__':main()
