#!/usr/bin/env python3
"""Original CoreAudio ARM caller bytes -> compiler-produced pdev ABI callees."""
import hashlib
import importlib.util
import json
import mmap
import struct
import sys
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM


def main():
    e = Evidence("audio-pdev-abi")
    spec = importlib.util.spec_from_file_location("pdev_build", REPO / "userland/audio/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    obj, image = e.out / "abi.o", e.out / "abi.dylib"
    probe = REPO / "port-tests/i9100_pdev_abi_probe.c"
    e.run(["clang", "--target=armv7-apple-ios6.1", "-mthumb", "-mfloat-abi=softfp", "-mfpu=neon", "-ffreestanding",
           "-fno-stack-protector", "-O1", "-Wall", "-Wextra", "-Werror", "-I" + str(PLATFORM), "-c", str(probe), "-o", str(obj)], "compile-abi-callees")
    e.run([str(ROOT / "assets/i9100-toolchain/bin/ld64"), "-arch", "armv7", "-dylib", "-ios_version_min", "6.1",
           "-install_name", "/org.i9100/abi-oracle", str(obj), "-o", str(image)], "link-abi-callees")
    sys.path.insert(0, str(ROOT / "assets/py-native-hid"))
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    u.mem_map(0, 0x80000)
    u.reg_write(UC_ARM_REG_C1_C0_2, 0x00f00000)
    u.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
    raw = image.read_bytes()
    at = 28
    for _ in range(struct.unpack_from("<I", raw, 16)[0]):
        cmd, size = struct.unpack_from("<II", raw, at)
        if cmd == 1:
            va, extent, offset, n = struct.unpack_from("<4I", raw, at + 24)
            if va + extent > 0x10000:
                raise RuntimeError("ABI image exceeded its fixture")
            if n:u.mem_write(va, raw[offset:offset+n])
        at += size
    exported = build.symbols(image)
    record = exported["_i9100_pdev_abi_record"]["value"]
    spec = importlib.util.spec_from_file_location("abi_cache", ROOT / "uniloader-S2I9100/scripts/i9100/symbolize-ios6-stacks.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cache_path = ROOT / "assets/ios6-extract/dyld_cache6_armv7"
    native = []
    with cache_path.open("rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        cache = module.Cache(data)
        for va in (0x30e4a000, 0x30e8b000, 0x30e8c000):
            u.mem_map(va, 4096)
            blob = data[cache.offset(va, 4096):cache.offset(va, 4096)+4096]
            u.mem_write(va, blob)
            native.append(dict(va=hex(va), sha256=hashlib.sha256(blob).hexdigest()))
    object_address, stack, stop = 0x10000, 0x70000, 0x7f000
    regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
    def write(address,*words):u.mem_write(address,struct.pack("<"+"I"*len(words),*words))
    def invoke(name,va,slot,args):
        fn=exported["_PhysicalDevicePlugIn_"+name]["value"]
        write(object_address+slot,fn|1)
        u.mem_write(record,bytes(48));write(stack,*args[4:],*([0]*16))
        for reg,value in zip(regs,args[:4]+[0]*(4-min(4,len(args)))):u.reg_write(reg,value)
        u.reg_write(UC_ARM_REG_SP,stack);u.reg_write(UC_ARM_REG_LR,stop|1)
        u.emu_start(va|1,stop,count=20000)
        if u.reg_read(UC_ARM_REG_PC)!=stop:raise RuntimeError("Original caller did not return")
        return list(struct.unpack("<12I",u.mem_read(record,48))),u.reg_read(UC_ARM_REG_R0)
    cases=[]
    rec,result=invoke("Initialize",0x30e4a338,0x28,[object_address]);assert rec[0]==object_address+12 and result==0;cases.append("Initialize host+12")
    rec,result=invoke("Teardown",0x30e8bc50,0x2c,[object_address]);assert rec[0]==0x54454152 and result==0;cases.append("Teardown")
    rec,result=invoke("ObjectHasProperty",0x30e8bc84,0x30,[object_address,123,0x12000]);assert rec[:2]==[123,0x12000] and result==1;cases.append("HasProperty Boolean")
    rec,result=invoke("ObjectIsPropertySettable",0x30e8bc98,0x34,[object_address,123,0x12000]);assert rec[:2]==[123,0x12000] and result==1;cases.append("IsSettable output byte")
    rec,result=invoke("ObjectGetPropertyDataSize",0x30e8bce4,0x38,[object_address,123,0x12000,19,0x13000]);assert rec[:4]==[123,0x12000,19,0x13000] and result==0x5a;cases.append("GetSize pointer/result")
    write(0x14000,128)
    rec,result=invoke("ObjectGetPropertyData",0x30e8bd30,0x3c,[object_address,123,0x12000,19,0x13000,0x14000,0x15000]);assert rec[:6]==[123,0x12000,19,0x13000,0x14000,0x15000] and result==0;assert bytes(u.mem_read(0x14000,4))==struct.pack("<I",4);cases.append("GetData length pointer")
    rec,result=invoke("ObjectSetPropertyData",0x30e8bd88,0x40,[object_address,123,0x12000,19,0x13000,77,0x15000]);assert rec[:6]==[123,0x12000,19,0x13000,77,0x15000] and result==0;cases.append("SetData length value")
    rec,result=invoke("GetZeroTimeStamp",0x30e8bde0,0x44,[object_address,123,0x14000,0x14010,0x14018]);assert rec[:4]==[123,0x14000,0x14010,0x14018] and result==0;assert struct.unpack("<d",u.mem_read(0x14000,8))[0]==123.0;assert struct.unpack("<Q",u.mem_read(0x14010,8))[0]==0xabcdef9012345678;cases.append("ZeroTimeStamp three output pointers")
    times=list(struct.unpack("<4I",struct.pack("<dd",1.25,8.75)))
    for name,va,slot in (("BeginIOCycle",0x30e8be20,0x48),("EndIOCycle",0x30e8bfc4,0x5c)):
        rec,result=invoke(name,va,slot,[object_address,123,456,64,0xabc,*times]);assert rec[:4]==[123,456,64,0xabc] and rec[8:12]==times and result==0;cases.append(name+" two doubles")
    rec,result=invoke("AcquireIOBufferForStream",0x30e8be70,0x4c,[object_address,123,456,64,0xabc,*times])
    assert rec[:4]==[123,456,64,0xabc] and rec[8:12]==times and result==0xdeadbeef
    assert stack-512<=rec[4]<stack;cases.append("Acquire seventh output pointer / C++ buffer result")
    for name,va,slot in (("ReadFromStream",0x30e8bec8,0x50),
                         ("WriteToStream",0x30e8bf1c,0x54),("ReleaseIOBufferForStream",0x30e8bf70,0x58)):
        rec,result=invoke(name,va,slot,[object_address,123,456,64,0xabc,*times,0x16000]);assert rec[:5]==[123,456,64,0xabc,0x16000] and rec[8:12]==times and result==0;cases.append(name+" seventh buffer argument")
    rec,result=invoke("PerformDeviceConfigChange",0x30e8c014,0x60,[object_address,123,456]);assert rec[:2]==[123,456] and result==0;cases.append("ConfigChange")
    e.result.update(original_caller_pages=native,original_cache=str(cache_path.relative_to(ROOT)),
                    abi_cases=cases,compiler_oracle=True,probe_sha256=hashlib.sha256(probe.read_bytes()).hexdigest(),
                    header_sha256=hashlib.sha256((REPO/"userland/audio/i9100-pdev.h").read_bytes()).hexdigest())
    e.finish([],"15 original CoreAudio ARM caller paths invoke compiler-produced pdev prototype oracles; no framework runtime/phone/audio execution")


if __name__=="__main__":main()
