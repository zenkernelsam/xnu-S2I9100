#!/usr/bin/env python3
"""Execute actual internal-root boot owner with native/MMC/HFS contract, host boundaries only."""
import argparse
import hashlib
import os
from i9100_offline_build import Evidence,REPO,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
e=Evidence('emmc-boot-offline');e.wait_idle_seconds=60
source=ROOT/'assets/i9100-backup/mmcblk0-head1mb.img'
if hashlib.sha256(source.read_bytes()).hexdigest()!='879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef':raise RuntimeError('Historical primary source changed')
os.environ['I9100_EMMC_HISTORICAL_SOURCE']=str(source)
os.environ['I9100_EMMC_BOOT_MODEL_RECORD']=str(e.out/'actual-contract.bin')
os.environ['I9100_EMMC_BOOT_MODEL_EVIDENCE']=str(e.out)
for name in ('i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c'):
    (e.out/name).write_bytes((REPO/'port-tests'/name).read_bytes())
c=['i9100_emmc.c','i9100_emmc_layout.c','i9100_emmc_writer.c','i9100_emmc_host.c','i9100_emmc_boot_contract.c','i9100_emmc_clock.c']
c+=['i9100_gpio_broker.c','i9100_clock_gates.c','i9100_irq_registry.c','i9100_peripheral_board.c','i9100_irq_cascade.c',
    'i9100_i2c_samsung.c','i9100_uart_bluetooth.c','i9100_sdio.c','i9100_bluetooth_h4.c','i9100_bcm_f2.c']
cpp=['i9100_emmc_native.cpp','i9100_emmc_worker.cpp','i9100_camera_mmio.cpp','IOS6I9100EMMCStorage.cpp','IOS6I9100EMMCRaw.cpp','IOS6I9100EMMCBlockDriver.cpp',
     'i9100_emmc_alias.cpp','i9100_emmc_publication.cpp','i9100_emmc_boot.cpp','i9100_peripheral_native.cpp']
c += ['i9100_emmc_idmac.c','i9100_dma_guard.c'];cpp += ['i9100_dma_native.cpp']
from i9100_emmc_dma_model import prepare as prepare_dma
dma_copies=prepare_dma(e)
e.model_cpp('i9100_emmc_boot_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
 source_overrides={'i9100_emmc_native_fixture.c':e.out/'i9100_emmc_native_fixture.c',**dma_copies},
 wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),
 extra_sdk=REPO/'port-tests/emmc-storage-sdk',libraries=('pthread',))
os.environ['I9100_EMMC_BOOT_MODEL_SDR_RECORD']=str(e.out/'actual-contract-sdr.bin')
e.model_cpp('i9100_emmc_boot_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
 source_overrides={'i9100_emmc_native_fixture.c':e.out/'i9100_emmc_native_fixture.c',**dma_copies},
 wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),
 extra_sdk=REPO/'port-tests/emmc-storage-sdk',libraries=('pthread',),extra_defines=('I9100_EMMC_SDR_BOOT_MODEL=1',))
e.result['sdr_boot_path']=True;e.result['actual_sdr_contract_sha256']=hashlib.sha256((e.out/'actual-contract-sdr.bin').read_bytes()).hexdigest()
os.environ['I9100_EMMC_BOOT_MODEL_IDMAC_RECORD']=str(e.out/'actual-contract-idmac.bin')
e.model_cpp('i9100_emmc_boot_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
 source_overrides={'i9100_emmc_native_fixture.c':e.out/'i9100_emmc_native_fixture.c',**dma_copies},
 wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),extra_sdk=REPO/'port-tests/emmc-storage-sdk',
 libraries=('pthread',),extra_defines=('I9100_EMMC_IDMAC_BOOT_MODEL=1',))
e.result['idmac_boot_path']=True;e.result['actual_idmac_contract_sha256']=hashlib.sha256((e.out/'actual-contract-idmac.bin').read_bytes()).hexdigest()
os.environ['I9100_EMMC_BOOT_MODEL_IDMAC_RECORD']=str(e.out/'actual-contract-idmac-sdr.bin')
e.model_cpp('i9100_emmc_boot_test.cpp',c+['i9100_emmc_native_fixture.c'],cpp,
 source_overrides={'i9100_emmc_native_fixture.c':e.out/'i9100_emmc_native_fixture.c',**dma_copies},
 wrapped_functions=('i9100_camera_mmio_read','i9100_camera_mmio_write'),extra_sdk=REPO/'port-tests/emmc-storage-sdk',
 libraries=('pthread',),extra_defines=('I9100_EMMC_IDMAC_BOOT_MODEL=1','I9100_EMMC_IDMAC_SDR_BOOT_MODEL=1',))
e.result['idmac_sdr_boot_path']=True;e.result['actual_idmac_sdr_contract_sha256']=hashlib.sha256((e.out/'actual-contract-idmac-sdr.bin').read_bytes()).hexdigest()
e.result['sdr_companion_header_sha256']=hashlib.sha256((REPO/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_emmc_sdr.h').read_bytes()).hexdigest()
e.result['nested_fixtures']={n:hashlib.sha256((REPO/'port-tests'/n).read_bytes()).hexdigest() for n in
 ('i9100_emmc_storage_test.cpp','i9100_emmc_native_test.cpp','i9100_emmc_native_fixture.c','i9100_emmc_writer_test.c','i9100_emmc_test.c')}
e.result['workloop_facade_sha256']=hashlib.sha256((REPO/'port-tests/i9100_emmc_workloop_facade.inc').read_bytes()).hexdigest()
e.result['actual_contract_sha256']=hashlib.sha256((e.out/'actual-contract.bin').read_bytes()).hexdigest()
e.result['model_table_evidence_sha256']={n:hashlib.sha256((e.out/n).read_bytes()).hexdigest() for n in ('cid.bin','gpt-primary.bin','gpt-backup.bin','pit.bin')}
if a.cross_compile:e.cross_compile(c+cpp)
e.finish(c+cpp,'Actual boot owner/contract and native/MMC/GPT/PIT/HFS-ID/publication control flow with retained failures; explicit MMIO/card/IOKit/catalogue facades, no actual registry/HFS mount/device/deployment')
