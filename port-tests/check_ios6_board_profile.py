#!/usr/bin/env python3
"""Check the production model provider and the original Classic board parser.

OSData/boot arguments and libc imports are modeled. The original ARM parser
executes; this does not establish a working modem or real SpringBoard UI.
"""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                         sys.executable, '-B', __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import json
import struct
import subprocess

repo = Path(__file__).resolve().parents[1]
root = repo.parents[1]
out = repo / 'out' / ('ios6-board-profile-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
text = (repo / 'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/AppleARMPE.cpp').read_text()
start = text.index('bool AppleARMPE::getModelName(')
brace = text.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
production = text[start:end]
source = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define BOARD_CONFIG_EXYNOS4210_I9100 1
static int abi=6;
static size_t strlcpy(char*dst,const char*src,size_t size){size_t n=strlen(src);if(size){unsigned take=n<size-1?n:size-1;memcpy(dst,src,take);dst[take]=0;}return n;}
struct Base {bool getModelName(char*p,int n){strlcpy(p,"samsung,i9100",n);return true;}};
struct AppleARMPE:Base {typedef Base super;bool getModelName(char*,int);};
static void PE_parse_boot_argn(const char*key,int*out,unsigned n){assert(!strcmp(key,"i9100hidabi")&&n==sizeof(int));*out=abi;}
''' + production + r'''
int main(){AppleARMPE p;char name[64];assert(p.getModelName(name,64)&&!strcmp(name,"N90AP"));
 FILE*f=fopen("compat-model.txt","wb");assert(f);fwrite(name,1,strlen(name),f);fclose(f);
 assert(!p.getModelName(0,64));assert(!p.getModelName(name,0));char guard[4]={1,2,3,4};
 assert(p.getModelName(guard+1,1));assert(guard[0]==1&&guard[1]==0&&guard[2]==3&&guard[3]==4);
 abi=7;assert(p.getModelName(name,64)&&!strcmp(name,"samsung,i9100"));
 abi=0;assert(p.getModelName(name,64)&&!strcmp(name,"samsung,i9100"));
 puts("PASS production iOS6 board profile, bounds and native fallback");}
'''
(out / 'check.cpp').write_text(source)
subprocess.run(['c++', '-O1', '-fsanitize=address,undefined', str(out / 'check.cpp'), '-o', str(out / 'check')], check=True)
subprocess.run([str(out / 'check')], cwd=out, check=True)
sys.path.insert(0, str(root / 'assets/py-native-hid'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import *

binary = (root / 'research/commcenter-20261113/CommCenterClassic').read_bytes()
HEAP = 0x10000000
STOP = HEAP + 0x1fff0


def execute(model):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    cpu.mem_map(0x1000, 0x1f1000)
    cpu.mem_write(0x1000, binary[:0x1f1000])
    cpu.mem_map(HEAP, 0x20000)
    cpu.mem_write(HEAP, model.encode() + b'\0')

    def string(at):
        data = bytearray()
        for n in range(128):
            byte = bytes(cpu.mem_read(at + n, 1))
            if byte == b'\0':
                return bytes(data)
            data += byte
        raise ValueError('Unterminated modeled libc input')

    def ret(value):
        lr = cpu.reg_read(UC_ARM_REG_LR)
        cpu.reg_write(UC_ARM_REG_R0, value & 0xffffffff)
        cpu.reg_write(UC_ARM_REG_PC, lr)

    def hook(uc, at, size, unused):
        r0, r1, r2 = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
        if at == 0x19e9f8:
            ret(HEAP + 0x400)
        elif at == 0x19f498:
            raw = string(r0)
            n = 0
            while n < len(raw) and 48 <= raw[n] <= 57:
                n += 1
            uc.mem_write(r1, struct.pack('<I', r0 + n))
            ret(int(raw[:n] or b'0', 10))
        elif at == 0x19f3d8:
            ret(len(string(r0)))
        elif at == 0x19f378:
            a, b = string(r0), string(r1)
            ret(0 if a == b else -1 if a < b else 1)
        elif at == 0x19f3c8:
            raw = string(r1)
            if r2:
                uc.mem_write(r0, raw[:r2 - 1] + b'\0')
            ret(len(raw))
        elif at == STOP:
            uc.emu_stop()
    cpu.hook_add(UC_HOOK_CODE, hook)
    cpu.reg_write(UC_ARM_REG_R0, HEAP)
    cpu.reg_write(UC_ARM_REG_R1, HEAP + 0x100)
    cpu.reg_write(UC_ARM_REG_SP, HEAP + 0x1f000)
    cpu.reg_write(UC_ARM_REG_LR, STOP | 1)
    cpu.emu_start(0x1944ad, STOP, count=10000)
    assert cpu.reg_read(UC_ARM_REG_PC) == STOP
    words = struct.unpack('<3I', cpu.mem_read(HEAP + 0x100, 12))
    return dict(model=model, accepted=cpu.reg_read(UC_ARM_REG_R0), number=words[0],
                board_letter=chr(words[1] & 255) if words[1] else '', suffix_kind=words[2])


old = execute('samsung,i9100')
new = execute((out / 'compat-model.txt').read_text())
assert old['accepted'] == 0
assert new == dict(model='N90AP', accepted=1, number=90, board_letter='N', suffix_kind=2)
result = dict(passed=True, old=old, new=new, scope=__doc__, phone_operations=0)
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
print('EVIDENCE', out)
