#!/usr/bin/env python3
"""Execute actual mount_common/VFS_MOUNT/boot helpers, facade only at vnode/disk boundaries."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO
from i9100_hfs_boot_copy import prepare,verify,compile_bsd
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('hfs-boot-model');e.wait_idle_seconds=60;copies=prepare(e)

def function(text,signature):
    start=text.index(signature);brace=text.index('{',start);depth=0;quote=None;comment=None;escape=False;i=brace
    while i<len(text):
        c=text[i];n=text[i+1:i+2]
        if comment=='line':
            if c=='\n':comment=None
        elif comment=='block':
            if c=='*' and n=='/':comment=None;i+=1
        elif quote:
            if escape:escape=False
            elif c=='\\':escape=True
            elif c==quote:quote=None
        elif c=='/' and n=='/':comment='line';i+=1
        elif c=='/' and n=='*':comment='block';i+=1
        elif c in ('"',"'"):quote=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return text[start:i+1]
        i+=1
    raise RuntimeError('Function extraction incomplete')

vfs=function(copies['vfs_syscalls.c'].read_text(),'static int\nmount_common(')
kpi=function((REPO/'bsd/vfs/kpi_vfs.c').read_text(),'int \nVFS_MOUNT(')
hfs=copies['hfs_vfsops.c'].read_text();start=hfs.index('int\nhfs_mount(');cut=hfs.index('\tcmdflags =',start)
prologue=hfs[start:cut]
model=e.out/'actual-vfs-model.c'
model.write_text('#include "i9100_hfs_boot_model.h"\n#include "i9100_hfs_mount_common.inc"\n'+
    kpi+'\n'+prologue+' (void)p;(void)hfsmp;(void)cmdflags;return model_hfs_disk_mount(mp,devvp,&args);\n}\n'+
    vfs+'\n#include "i9100_hfs_data_boot.inc"\n#include "i9100_hfs_shutdown.inc"\n#include "i9100_hfs_boot_test.c"\n')
e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-missing-field-initializers',
 '-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(REPO/'port-tests'),
 '-I'+str(REPO/'bsd/hfs'),'-I'+str(REPO/'bsd/vfs'),'-I'+str(REPO/'bsd/kern'),str(model),'-o',str(e.out/'models')],'compile-actual-vfs-model')
e.run([str(e.out/'models')],'actual-vfs-model-tests')
e.result['fixture_sources']={name:hashlib.sha256((REPO/name).read_bytes()).hexdigest() for name in
 ('port-tests/i9100_hfs_boot_model.h','port-tests/i9100_hfs_boot_test.c','bsd/vfs/kpi_vfs.c')}
e.result['extracted_production']=dict(source_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),
    functions=['mount_common','VFS_MOUNT','hfs_mount argument prologue'],
    original_hfs_disk_body_executed=False,vnode_and_registry_facade=True)
e.result['model_artifact_sha256']=hashlib.sha256((e.out/'models').read_bytes()).hexdigest()
if a.cross_compile:compile_bsd(e,copies)
verify(e);e.finish([], 'Actual extracted VFS and original HFS arg path+production early mount helpers; explicit vnode/registry/HFSdisk facade; no actual XNU boot, mount, hardware or persistence')
