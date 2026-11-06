#!/usr/bin/env python3
import argparse
import capstone
p=argparse.ArgumentParser()
p.add_argument('image');p.add_argument('address',type=lambda s:int(s,0))
p.add_argument('--base',type=lambda s:int(s,0),default=0x80001000)
p.add_argument('--size',type=lambda s:int(s,0),default=0x180)
p.add_argument('--arm',action='store_true')
a=p.parse_args()
with open(a.image,'rb') as f:
    f.seek(a.address-a.base);data=f.read(a.size)
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM if a.arm else capstone.CS_MODE_THUMB)
for i in md.disasm(data,a.address): print('%08x %-9s %s'%(i.address,i.mnemonic,i.op_str))
