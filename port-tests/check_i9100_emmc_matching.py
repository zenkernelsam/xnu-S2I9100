#!/usr/bin/env python3
"""Execute original XNU matching functions with actual default-OFF eMMC providers."""
import argparse
import hashlib
import os
import ast
import json
import re
import xml.etree.ElementTree as ET
from i9100_offline_build import Evidence,REPO,ROOT

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
    raise RuntimeError('Production function extraction incomplete')

def prepare(e):
    paths=['iokit/Kernel/IOService.cpp','iokit/Kernel/Storage/IOMedia.cpp','iokit/Kernel/IOServicePrivate.h',
           'iokit/Kernel/Storage/IOBlockStorageDriver.cpp','iokit/Kernel/Storage/IOMediaBSDClient.cpp']
    originals={name:(REPO/name).read_bytes() for name in paths}
    for key,value in (('kIOServiceChangesOK',4),('kIOServiceInternalDone',8),('kIOServiceClassDone',16)):
        actual_constant=re.search(r'\b'+key+r'\s*=\s*(0x[0-9a-fA-F]+)',originals[paths[2]].decode())
        if not actual_constant or int(actual_constant[1],0)!=value:raise RuntimeError('Actual matching option constants changed')
    signatures=['bool IOService::matchInternal(','bool IOService::matchPassive(',
                'bool IOService::compareProperty( OSDictionary * matching,',
                'bool IOService::matchPropertyTable( OSDictionary * table, SInt32 * score )',
                'bool IOService::matchPropertyTable( OSDictionary * table )']
    # Original no-op IOService family methods intentionally have unused
    # parameters; keep the production bodies intact under a local pragma.
    bodies=['#pragma clang diagnostic push\n#pragma clang diagnostic ignored "-Wunused-parameter"']
    bodies += [function(originals[paths[0]].decode(),s) for s in signatures]
    # IOStorage inherits IOService's matching; the host storage shell does
    # not emulate its unrelated DMA/open methods here.
    bodies+=['#define super IOService\n'+function(originals[paths[1]].decode(),'bool IOMedia::matchPropertyTable(')+'\n#undef super']
    bodies+=[function(originals[paths[3]].decode(),'IOMedia *\nIOBlockStorageDriver::instantiateMediaObject('),
             function(originals[paths[4]].decode(),'IOMedia * IOMediaBSDClient::getWholeMedia(')]
    bodies+=['#pragma clang diagnostic pop']
    folder=e.out/'matching-fixture';folder.mkdir()
    nested=('i9100_emmc_boot_test.cpp','i9100_emmc_storage_test.cpp','i9100_emmc_native_test.cpp',
            'i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c')
    nested=nested+('i9100_emmc_workloop_facade.inc',)
    for name in nested:(folder/name).write_bytes((REPO/'port-tests'/name).read_bytes())
    model=folder/'i9100_emmc_matching_test.cpp'
    fixture=(REPO/'port-tests/i9100_emmc_matching_test.cpp').read_bytes()
    model.write_bytes(fixture+b'\n'+('\n'.join(bodies)+'\n').encode())
    publication=REPO/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_emmc_publication.cpp'
    actual=publication.read_bytes()
    declaration=re.search(r'static const char personalities\[\]\s*=([\s\S]*?);',actual.decode())
    if not declaration:raise RuntimeError('Missing actual scoped personalities')
    strings=re.findall(r'"(?:[^"\\]|\\.)*"',declaration[1])
    xml=''.join(ast.literal_eval(s) for s in strings);tree=ET.fromstring(xml)
    if tree.tag!='array' or len(tree)!=3:raise RuntimeError('Unexpected personality array')
    def dictionary(node,name):
        if node.tag!='dict' or len(node)%2:raise RuntimeError('Invalid personality dictionary')
        lines=[f'auto *{name}=OSDictionary::withCapacity({len(node)//2});']
        keys=set()
        for index in range(0,len(node),2):
            key,value=node[index:index+2]
            if key.tag!='key' or not key.text or key.text in keys:raise RuntimeError('Duplicate/missing personality key')
            keys.add(key.text);k=json.dumps(key.text)
            if value.tag=='dict':
                child=f'{name}_{index}';lines+=dictionary(value,child)
                lines += [f'assert({name}->setObject({k},{child}));{child}->release();']
            elif value.tag=='string':lines += [f'put_string({name},{k},{json.dumps(value.text)});']
            elif value.tag=='integer':lines += [f'put_number({name},{k},{int(value.text,0)}U);']
            elif value.tag in ('true','false'):lines += [f'put_bool({name},{k},{value.tag});']
            else:raise RuntimeError('Unsupported personality element')
        return lines
    generated=['static OSDictionary *actual_personality(unsigned index){switch(index){']
    for index,node in enumerate(tree):generated += [f'case {index}:{{',*dictionary(node,'table'),'return table;}']
    generated+=['default:return nullptr;}}']
    (folder/'actual-personalities.inc').write_text('\n'.join(generated)+'\n')
    e.result['actual_matching']=dict(functions=signatures+['bool IOMedia::matchPropertyTable(',
      'IOMedia * IOBlockStorageDriver::instantiateMediaObject(','IOMedia * IOMediaBSDClient::getWholeMedia('],
      source_files={name:hashlib.sha256(raw).hexdigest() for name,raw in originals.items()},
      model_source=str(model.relative_to(ROOT)),model_source_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),
      fixture_sha256=hashlib.sha256(fixture).hexdigest(),personality_source_sha256=hashlib.sha256(actual).hexdigest(),
      personality_xml_sha256=hashlib.sha256(xml.encode()).hexdigest(),
      personality_fixture_sha256=hashlib.sha256((model.parent/'actual-personalities.inc').read_bytes()).hexdigest(),
      original_block_instantiation_and_bsd_whole_search=True,scheduler_and_registry_facade=True,hfs_mount_executed=False)
    e.result['nested_fixtures']={name:hashlib.sha256((REPO/'port-tests'/name).read_bytes()).hexdigest() for name in nested}
    return model,originals,actual

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
    e=Evidence('emmc-matching-offline');e.wait_idle_seconds=60
    model,originals,actual=prepare(e)
    history=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
    if hashlib.sha256(history.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical table drift')
    os.environ['I9100_EMMC_HISTORICAL_SOURCE']=str(history)
    c=['i9100_emmc.c','i9100_emmc_layout.c','i9100_emmc_writer.c','i9100_emmc_host.c','i9100_emmc_boot_contract.c','i9100_emmc_clock.c',
       'i9100_gpio_broker.c','i9100_clock_gates.c','i9100_irq_registry.c','i9100_peripheral_board.c','i9100_irq_cascade.c',
       'i9100_i2c_samsung.c','i9100_uart_bluetooth.c','i9100_sdio.c','i9100_bluetooth_h4.c','i9100_bcm_f2.c']
    cpp=['i9100_emmc_native.cpp','i9100_emmc_worker.cpp','i9100_camera_mmio.cpp','IOS6I9100EMMCStorage.cpp','IOS6I9100EMMCRaw.cpp',
         'IOS6I9100EMMCBlockDriver.cpp','i9100_emmc_alias.cpp','i9100_emmc_publication.cpp',
         'i9100_emmc_boot.cpp','i9100_peripheral_native.cpp']
    c += ['i9100_emmc_idmac.c','i9100_dma_guard.c'];cpp += ['i9100_dma_native.cpp']
    from i9100_emmc_dma_model import prepare as prepare_dma
    dma_copies=prepare_dma(e)
    (model.parent/'i9100_emmc_dma_facade.inc').write_bytes((REPO/'port-tests/i9100_emmc_dma_facade.inc').read_bytes())
    e.model_cpp('i9100_emmc_matching_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
       source_overrides={'i9100_emmc_native_fixture.c':model.parent/'i9100_emmc_native_fixture.c',**dma_copies},
       wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),
       extra_sdk=REPO/'port-tests/emmc-storage-sdk',libraries=('pthread',),
       test_override=model,extra_defines=('I9100_EMMC_MATCH_ENGINE=1',))
    if a.cross_compile:e.cross_compile(c+cpp)
    if any((REPO/name).read_bytes()!=raw for name,raw in originals.items()):raise RuntimeError('Matching source drift')
    if (REPO/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_emmc_publication.cpp').read_bytes()!=actual:raise RuntimeError('Publication XML source drift')
    e.finish(c+cpp,'Original XNU matching executed with actual raw/block/whole/pair modules; allocator/card/IOKit scheduling facades, no actual boot/HFS/journal or hardware')
if __name__=='__main__':main()
