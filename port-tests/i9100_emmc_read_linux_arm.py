#!/usr/bin/env python3
"""Execute the exact Linux ARM ELF with syscall/card boundaries explicitly modeled."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'assets/py-native-hid'))
sys.dont_write_bytecode=True
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_INTR
from unicorn.arm_const import *
NONCE='0102030405060708090a0b0c0d0e0f10';CID='1501000053324939313030009a73abcd'

def load_tables(proof):
    stream=(proof/'inventory.stream').read_bytes();tables={};at=128
    while at<len(stream):
        kind,n,offset=struct.unpack_from('<IIQ',stream,at+8);kind&=65535
        body=stream[at+32:at+32+n]
        if 1<=kind<=6:tables[offset]=body
        at+=32+n
    return tables

class LinuxBoundary:
    def __init__(self,raw,tables,out,case='inventory',fault=None):
        self.u=Uc(UC_ARCH_ARM,UC_MODE_ARM);self.u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        self.fds={};self.next_fd=10;self.calls=[];self.reads=[];self.out=out;self.fault=fault;self.case=case
        self.tables=tables;self.position=0;self.exit=None;self.high_read=0;self.starts=0;self.eof=False
        entry,phoff=struct.unpack_from('<II',raw,24);phsize,phnum=struct.unpack_from('<HH',raw,42)
        pages=set()
        for i in range(phnum):
            kind,off,va,pa,filesz,memsz,flags,align=struct.unpack_from('<8I',raw,phoff+i*phsize)
            if kind!=1:continue
            for at in range(va&~4095,(va+memsz+4095)&~4095,4096):
                if at not in pages:self.u.mem_map(at,4096);pages.add(at)
            self.u.mem_write(va,raw[off:off+filesz])
        self.entry=entry;self.u.mem_map(0x70000000,2*1024*1024);sp=0x701ff000;addr=0x70001000
        args=['emmc-read-linux','inventory',NONCE] if case=='inventory' else ['emmc-read-linux','backup-user',NONCE,CID]
        pointers=[]
        for item in args:
            pointers.append(addr);encoded=item.encode()+b'\0';self.u.mem_write(addr,encoded);addr+=len(encoded)
        self.u.mem_write(sp,struct.pack('<'+str(len(args)+2)+'I',len(args),*pointers,0));self.u.reg_write(UC_ARM_REG_SP,sp)
        self.u.hook_add(UC_HOOK_INTR,self.syscall)
        self.fd_identity_count=0
    def cstr(self,at):
        out=bytearray()
        for i in range(256):
            ch=self.u.mem_read(at+i,1)[0]
            if not ch:return out.decode()
            out.append(ch)
        raise AssertionError('Unbounded path')
    def result(self,value):self.u.reg_write(UC_ARM_REG_R0,value&0xffffffff)
    def file_bytes(self,path):
        fixed={'/sys/class/block/mmcblk0/dev':b'179:0\n','/sys/class/block/mmcblk0/device/cid':(CID+'\n').encode(),
          '/sys/class/block/mmcblk0/size':b'30777344\n','/sys/class/block/mmcblk0/device/type':b'MMC\n',
          '/sys/class/block/mmcblk0/queue/logical_block_size':b'512\n',
          '/proc/self/mountinfo':b'10 1 0:10 / / rw - tmpfs tmpfs rw\n',
          '/proc/swaps':b'Filename\tType\tSize\tUsed\tPriority\n',
          '/proc/iomem':b'12550000-12550fff : s3c-mshci\n'}
        if path=='/proc/self/mountinfo' and self.fault=='mounted':return b'10 1 179:9 / /system rw - ext4 mmcblk0p9 rw\n'
        if path=='/proc/self/mountinfo' and self.fault=='external-mount':return b'10 1 179:33 / /external_sd rw - vfat mmcblk1p1 rw\n'
        if path=='/sys/class/block/mmcblk0/device/cid' and self.fault=='cid':
            self.fd_identity_count+=1
            if self.fd_identity_count>=2:return b'0501000053324939313030009a73abcd\n'
        return fixed.get(path)
    def block_bytes(self,offset,n):
        if self.fault=='read' or self.fault=='high-read' and offset>=0x100000000:return None
        out=bytearray(n)
        for at,body in self.tables.items():
            start=max(at,offset);end=min(at+len(body),offset+n)
            if start<end:out[start-offset:end-offset]=body[start-at:end-at]
        return bytes(out)
    def syscall(self,u,interrupt,user):
        nr=u.reg_read(UC_ARM_REG_R7);a=[u.reg_read(reg) for reg in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5)]
        self.calls.append((nr,*a))
        if nr==5:
            path=self.cstr(a[0]);flags=a[1]
            assert flags&3==0 and not flags&0o1100 and flags&0o400000 and flags&0o100000
            if path=='/dev/block/mmcblk0':
                assert flags&0o200 and a[2]==0
                if self.fault=='exclusive':self.result(-16);return
                body=None
            elif path.endswith('/holders'):
                assert flags&0o40000;body=b'directory'
            else:
                body=self.file_bytes(path)
                if body is None:self.result(-2);return
            fd=self.next_fd;self.next_fd+=1;self.fds[fd]=dict(path=path,body=body,pos=0);self.result(fd)
        elif nr==6:
            assert a[0]in self.fds;del self.fds[a[0]];self.result(0)
        elif nr==197:
            assert self.fds[a[0]]['path']=='/dev/block/mmcblk0'
            raw=bytearray(104);struct.pack_into('<I',raw,16,0o60000);struct.pack_into('<Q',raw,32,179<<8)
            u.mem_write(a[1],bytes(raw));self.result(0)
        elif nr==54:
            assert self.fds[a[0]]['path']=='/dev/block/mmcblk0'
            if a[1]==0x80041272:u.mem_write(a[2],struct.pack('<Q',30777344*512))
            elif a[1]==0x1268:u.mem_write(a[2],struct.pack('<I',512))
            else:raise AssertionError('Unexpected ioctl can alter device')
            self.result(0)
        elif nr==217:
            assert self.fds[a[0]]['path'].endswith('/holders')
            if self.fault=='holder' and self.fds[a[0]]['pos']==0:
                raw=bytearray(24);struct.pack_into('<H',raw,16,24);raw[19:24]=b'dm-0\0';u.mem_write(a[1],bytes(raw));self.fds[a[0]]['pos']=24;self.result(24)
            else:self.result(0)
        elif nr==140:
            assert self.fds[a[0]]['path']=='/dev/block/mmcblk0' and a[4]==0
            offset=(a[1]<<32)|a[2];self.position=offset;u.mem_write(a[3],struct.pack('<Q',offset));self.result(0)
        elif nr==3:
            fd=self.fds[a[0]];requested=a[2]
            if fd['path']=='/dev/block/mmcblk0':
                assert self.position+requested<=30777344*512
                self.reads.append((self.position,requested));body=self.block_bytes(self.position,requested)
                if body is None:self.high_read=self.position;self.result(-5);return
                u.mem_write(a[1],body);self.position+=requested;self.result(requested)
            else:
                body=fd['body'][fd['pos']:fd['pos']+requested]
                # Force text short reads so the production collector's
                # read loop must handle fragmented sysfs/proc data.
                body=body[:7];u.mem_write(a[1],body);fd['pos']+=len(body);self.result(len(body))
        elif nr==4:
            assert a[0]in (1,2)
            body=bytes(u.mem_read(a[1],a[2]))
            if a[0]==1:
                if self.fault=='output':self.result(-32);return
                self.out.write(body)
            self.result(len(body))
        elif nr==248:self.exit=a[0];u.emu_stop()
        else:raise AssertionError('Unexpected mutating/unmodeled syscall: '+str(nr))
    def run(self,count=30000000):
        self.u.emu_start(self.entry,0,count=count)
        if self.exit is None:raise AssertionError('ARM collector exceeded bounded instruction run')
        return dict(exit=self.exit,syscalls=len(self.calls),block_reads=len(self.reads),max_offset=max([p for p,n in self.reads]or[0]),
                    read_only_open=True,write_device_syscalls=0,remaining_descriptors=len(self.fds))

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--table-proof',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    build=a.build.resolve();raw=(build/'emmc-read-linux').read_bytes();m=json.loads((build/'manifest.json').read_text())
    if hashlib.sha256(raw).hexdigest()!=m['linux_arm_executable']['sha256']:raise ValueError('Actual collector binary changed')
    tables=load_tables(a.table_proof.resolve());a.output.mkdir()
    results={}
    for case,fault in [('inventory',None),('inventory','external-mount'),('inventory','exclusive'),('inventory','mounted'),('inventory','holder'),('inventory','cid'),('inventory','read'),('inventory','high-read'),('inventory','output')]:
        name=case+'-'+(fault or 'ok');path=a.output/(name+'.stream')
        with path.open('xb')as f:
            boundary=LinuxBoundary(raw,tables,f,case,fault);result=boundary.run()
        if result['exit']!=(0 if fault in (None,'external-mount') else 3 if fault=='exclusive' else 4) or result['remaining_descriptors']:
            (a.output/(name+'.failed.json')).write_text(json.dumps(dict(result=result,calls=boundary.calls),indent=2)+'\n')
            raise AssertionError(name+' failed: '+str(result))
        results[name]=result
    spec=importlib.util.spec_from_file_location('readimport',ROOT/'uniloader-S2I9100/scripts/i9100/emmc-read-evidence.py');importer=importlib.util.module_from_spec(spec);spec.loader.exec_module(importer)
    folder,record=importer.import_stream(a.output/'inventory-ok.stream',ROOT/'uniloader-S2I9100/out'/(a.output.parent.name+'-arm'),NONCE,
      'Exact production ARM ELF, explicit Linux syscall/memory/card boundaries, no actual phone',0,True)
    assert record['source_is_model']and record['model_fixture']and not record['device_observed']and not record['boot_ready']
    report=dict(result='PASS',hardware_tested=False,device_contacted=False,collector_sha256=hashlib.sha256(raw).hexdigest(),
      producer_source_sha256=record['capture_header']['producer_source_sha256'],
      test_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),instruction_set='Cortex-A9 ARM EABI',cases=results,
      imported_model=str(folder.relative_to(ROOT)),imported_manifest_sha256=hashlib.sha256((folder/'manifest.json').read_bytes()).hexdigest())
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS exact production ARM ELF read-only syscalls/shortreads/exclusive/mount/holder/CID/I-O/output failures; hardware modeled')
if __name__=='__main__':main()
