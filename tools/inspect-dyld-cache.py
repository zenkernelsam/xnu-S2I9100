#!/usr/bin/env python3
"""Resolve sampled slid PCs against an offline 32-bit dyld shared cache."""
import argparse
import json
import mmap
import struct

p = argparse.ArgumentParser()
p.add_argument('cache')
p.add_argument('--runtime-base',type=lambda s:int(s,0),required=True)
p.add_argument('--disassemble', action='store_true')
p.add_argument('--arm', action='store_true', help='ARM state (CPSR.T=0), instead of Thumb')
p.add_argument('pcs',nargs='+',type=lambda s:int(s,0))
a=p.parse_args()
with open(a.cache,'rb') as f:
    d=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
    mo,mc,io,ic=struct.unpack_from('<IIII',d,16)
    maps=[struct.unpack_from('<QQQII',d,mo+i*32) for i in range(mc)]
    slide=a.runtime_base-maps[0][0]
    def cstr(off): return d[off:d.find(b'\0',off)].decode('utf8','replace')
    def fileoff(va):
        for start,size,off,_,_ in maps:
            if start<=va<start+size: return off+va-start
        raise ValueError(hex(va))
    images=[]
    for i in range(ic):
        addr,_,_,path,_=struct.unpack_from('<QQQII',d,io+i*32)
        images.append((addr,cstr(path)))
    images.sort()
    result=[]
    for pc in a.pcs:
        va=pc-slide
        start,path=max((x for x in images if x[0]<=va),key=lambda x:x[0])
        header=fileoff(start)
        ncmds=struct.unpack_from('<I',d,header+16)[0]
        cur=header+28
        symbols=[]
        for _ in range(ncmds):
            cmd,size=struct.unpack_from('<II',d,cur)
            if cmd==2:
                so,sn,st,ss=struct.unpack_from('<IIII',d,cur+8)
                for n in range(sn):
                    si,typ,sec,desc,value=struct.unpack_from('<IBBHI',d,so+n*12)
                    if typ&0xe0==0 and typ&0x0e==0x0e and start<=value<=va and si<ss:
                        symbols.append((value,cstr(st+si)))
            cur+=size
        nearest=max(symbols,default=(start,'?'))
        result.append({'pc':hex(pc),'unslid':hex(va),'file_offset':hex(fileoff(va)),
            'image':path,'image_address':hex(start),'symbol':nearest[1],
            'symbol_address':hex(nearest[0]+slide),'displacement':hex(va-nearest[0])})
        if a.disassemble:
            import capstone
            md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM if a.arm else capstone.CS_MODE_THUMB)
            begin=max(nearest[0], va - 128)
            result[-1]['instructions']=['%08x %s %s'%(i.address,i.mnemonic,i.op_str)
                for i in md.disasm(d[fileoff(begin):fileoff(va)+4],begin)]
    print(json.dumps({'slide':hex(slide),'locations':result},indent=2))
