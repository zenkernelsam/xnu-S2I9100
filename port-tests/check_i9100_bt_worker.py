#!/usr/bin/env python3
"""True native UART/power/boot worker with a register-level peer and IOKit facade."""
import argparse
import hashlib
from i9100_offline_build import Evidence,REPO
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cross-compile',action='store_true');a=p.parse_args()
c=['i9100_gpio_broker.c','i9100_clock_gates.c','i9100_irq_registry.c','i9100_peripheral_board.c',
 'i9100_i2c_samsung.c','i9100_uart_bluetooth.c','i9100_sdio.c','i9100_radio_power.c','i9100_irq_cascade.c',
 'i9100_bluetooth_h4.c','i9100_bt_boot.c']
cpp=['i9100_radio_native.cpp','i9100_bt_worker.cpp']
e=Evidence('bt-worker-offline');e.wait_idle_seconds=60
e.model_cpp('i9100_bt_worker_test.cpp',c,cpp,libraries=['crypto'],wrapped_functions=('i9100_uart_bt_init',))
e.result['nested_fixtures']={name:hashlib.sha256((REPO/'port-tests'/name).read_bytes()).hexdigest() for name in ['i9100_radio_native_test.cpp','i9100_native_parent_model.h']}
if a.cross_compile:e.cross_compile(c+cpp+['i9100_peripheral_native.cpp'])
e.finish(c+cpp+['i9100_peripheral_native.cpp'],'Actual radio/native UART/H4/BCM bootstrap/workloop with register/ROM/IOKit/supply facades; no physical or iOS HCI proof')
