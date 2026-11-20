#!/usr/bin/env python3
"""Build a read-only Linux ARM EABI collector under CPU limits, no phone contacts."""
import hashlib
import json
import struct
from i9100_offline_build import Evidence,ROOT

e=Evidence('emmc-read-linux-build');e.wait_idle_seconds=60
repo=ROOT/'uniloader-S2I9100';source=repo/'scripts/i9100'
names=('emmc-read-linux.c','emmc-read-linux.S','emmc-read-stream.c','emmc-read-stream.h')
sha={n:hashlib.sha256((source/n).read_bytes()).hexdigest() for n in names}
pin=ROOT/'research/peripheral-reference-20261115/source-pin.json';tree=pin.with_name('source-tree.json')
catalogue=json.loads(tree.read_text());index={x['path']:x for x in catalogue['tree']if x.get('type')=='blob'}
vendor=ROOT/'research/vendor-linux-u1-a0346556/android_kernel_samsung_smdk4210-a0346556698e62405a04b010c4f4197bdc9fec06'
reference_names=('arch/arm/include/asm/stat.h','arch/arm/include/asm/fcntl.h','arch/arm/include/asm/unistd.h',
 'include/asm-generic/fcntl.h','include/asm-generic/ioctl.h','include/linux/fs.h','fs/block_dev.c','drivers/mmc/core/mmc.c')
refs={}
for name in reference_names:
    raw=(vendor/name).read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if not index.get(name)or index[name]['sha']!=blob:raise RuntimeError('Linux EABI/source reference differs from pinned original blob')
    refs[str((vendor/name).relative_to(ROOT))]=dict(sha256=hashlib.sha256(raw).hexdigest(),git_blob=blob)
probe=e.out/'linux-abi-probe.c'
probe.write_text('#include <stddef.h>\n#include "arch/arm/include/asm/stat.h"\n#include "arch/arm/include/asm/fcntl.h"\n'
 '#include "arch/arm/include/asm/unistd.h"\n'
 '_Static_assert(offsetof(struct stat64,st_mode)==16,"mode ABI");\n'
 '_Static_assert(offsetof(struct stat64,st_rdev)==32,"device ABI");\n'
 '_Static_assert(O_DIRECTORY==040000 && O_NOFOLLOW==0100000 && O_LARGEFILE==0400000 && O_EXCL==0200,"ARM flags");\n'
 '_Static_assert(__NR_read==3 && __NR_write==4 && __NR_open==5 && __NR_close==6 && __NR_ioctl==54 && __NR__llseek==140 && __NR_fstat64==197 && __NR_getdents64==217 && __NR_exit_group==248,"syscall ABI");\n')
e.run(['clang','--target=armv7a-linux-gnueabi','-mcpu=cortex-a9','-mfloat-abi=soft','-O1','-ffreestanding',
 '-I'+str(vendor),'-I'+str(vendor/'include'),'-c',str(probe),'-o',str(e.out/'linux-abi-probe.o')],'actual-pinned-linux-abi')
producer_tag=hashlib.sha256(json.dumps(sha,sort_keys=True,separators=(',',':')).encode()).hexdigest()
objects=[]
for name in names[:3]:
    out=e.out/(name+'.o')
    e.run(['clang','--target=armv7a-linux-gnueabi','-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-O1',
      '-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-fno-pic',
      '-fno-unwind-tables','-fno-asynchronous-unwind-tables',*(['-DI9RO_PRODUCER_SOURCE_HEX="'+producer_tag+'"']if name.endswith('.c')else []),
      '-I'+str(source),'-c',str(source/name),'-o',str(out)],'arm-'+name)
    objects.append(out)
binary=e.out/'emmc-read-linux'
e.run(['ld.lld','-m','armelf_linux_eabi','-static','-e','_start','--no-undefined',*map(str,objects),'-o',str(binary)],'static-linux-arm-link')
raw=binary.read_bytes()
if raw[:7]!=b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H',raw,18)[0]!=40:raise RuntimeError('Require ARM32LE ELF')
entry,phoff=struct.unpack_from('<II',raw,24);phsize,phnum=struct.unpack_from('<HH',raw,42)
if phsize!=32 or phnum>32 or any(struct.unpack_from('<I',raw,phoff+i*32)[0]in (2,3) for i in range(phnum)):
    raise RuntimeError('Collector must have no interpreter/dynamic dependency')
e.run(['readelf','-h','-l','-s',str(binary)],'elf-static-audit')
if any(hashlib.sha256((source/n).read_bytes()).hexdigest()!=s for n,s in sha.items()):raise RuntimeError('Read-only collector source drift')
e.result.update(result='PASS',device_contacted=False,linux_arm_executable=dict(path=str(binary.relative_to(ROOT)),
  sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),entry=entry,libc_required=False),
  collector_sources={str((source/n).relative_to(ROOT)):digest for n,digest in sha.items()},producer_source_sha256=producer_tag,
  original_linux_reference=refs,pinned_reference=dict(pin_sha256=hashlib.sha256(pin.read_bytes()).hexdigest(),
    tree_sha256=hashlib.sha256(tree.read_bytes()).hexdigest(),linux_abi_probe_sha256=hashlib.sha256(probe.read_bytes()).hexdigest()),
  scope='Real static Cortex-A9 Linux ARM ELF link only; not device runtime/MMC backup verification')
(e.out/'manifest.json').write_text(json.dumps(e.result,indent=2)+'\n');print('EVIDENCE',e.out)
