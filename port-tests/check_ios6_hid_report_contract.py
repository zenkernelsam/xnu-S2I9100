#!/usr/bin/env python3
"""Exercise original iOS6 filtering on retained event bytes and capabilities.

CF per-client monitor/request getters are modeled from the observed zero
request. Original IOKit filter/type/time instructions run on Cortex-A9.
This proves a protocol boundary, not real UI delivery.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import struct,mmap,json
repo=Path(__file__).resolve().parents[1];root=repo.parent
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE,UC_HOOK_MEM_UNMAPPED
from unicorn.arm_const import *
evidence=root/'uniloader-S2I9100/out/forensic/hid-20261115-2019-short-gesture-route/after-input/ui-observations.json'
rows=json.loads(evidence.read_text())['hid_execution_rows']['rows']
event=bytes.fromhex(next(r['raw_hex'] for r in rows if r['phase']==2))
assert struct.unpack_from('<I',event,36)[0]==11
out=repo/'out'/('hid-report-contract-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
results=[]
with (root/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as f:
 with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
  mo,mc=struct.unpack_from('<2I',data,16);maps=[struct.unpack_from('<QQQII',data,mo+n*32) for n in range(mc)]
  for advertised in (1,0):
   u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);u.cpr_write(15,0,1,0,2,0,False,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,1<<30)
   u.mem_map(0x20000000,0x20000);u.mem_map(0x21000000,0x20000)
   u.mem_write(0x21001000,event);u.mem_write(0x21000048,struct.pack('<2I',5500000,advertised))
   u.reg_write(UC_ARM_REG_CPSR,0x30);u.reg_write(UC_ARM_REG_SP,0x20010000);u.reg_write(UC_ARM_REG_LR,0x2001ffc1)
   for reg,v in ((UC_ARM_REG_R0,0x21002000),(UC_ARM_REG_R1,0x21000000),(UC_ARM_REG_R2,0x21001000)):u.reg_write(reg,v)
   calls=[]
   def page(uc,access,address,size,value,user):
    p=address&~4095
    for a,n,o,_,_ in maps:
     if a<=p and p+4096<=a+n:uc.mem_map(p,4096);uc.mem_write(p,data[o+p-a:o+p-a+4096]);return True
    return False
   def code(uc,address,size,user):
    if address==0x2001ffc0:uc.emu_stop();return
    if address in (0x31d265dc,0x31d26470,0x31d265ac):
     calls.append(address);uc.reg_write(UC_ARM_REG_R0,0);uc.reg_write(UC_ARM_REG_R1,0);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
   u.hook_add(UC_HOOK_MEM_UNMAPPED,page);u.hook_add(UC_HOOK_CODE,code)
   u.emu_start(0x31d244d9,0x2001ffc0,count=10000)
   result=u.reg_read(UC_ARM_REG_R0);assert result==(0 if advertised else 1)
   results.append(dict(report_interval_advertised=bool(advertised),requested_interval=0,original_filter_result=result,modeled_client_getters=[hex(x) for x in calls]))
(out/'result.json').write_text(json.dumps(dict(result='PASS',source=str(evidence),cases=results,ui_verified=False),indent=2)+'\n')
print('PASS original iOS6 Cortex-A9 filter rejects live zero-request advertised rate control and accepts the honest fixed-cadence capability')
print('EVIDENCE',out)
