#!/usr/bin/env python3
"""Actual sensor/worker/encoder collector and original current native interfaces."""
import argparse
import hashlib
import os
from i9100_offline_build import Evidence, REPO
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cross-compile',action='store_true')
p.add_argument('--wait-idle',type=int,default=60,choices=range(61))
a=p.parse_args()
c=['i9100_gpio_broker.c','i9100_clock_gates.c','i9100_irq_registry.c','i9100_peripheral_board.c',
   'i9100_i2c_samsung.c','i9100_uart_bluetooth.c','i9100_sdio.c','i9100_irq_cascade.c',
   'i9100_sensors.c','i9100_sensor_hid_wire.c']
cpp=['i9100_sensor_worker.cpp','i9100_sensor_hid.cpp']
e=Evidence('sensor-hid-offline');e.wait_idle_seconds=a.wait_idle
os.environ['I9100_SENSOR_HID_MODEL_OUTPUT']=str(e.out)
e.model('i9100_sensor_hid_wire_test.c',['i9100_sensor_hid_wire.c'])
e.model_cpp('i9100_sensor_hid_test.cpp',c,cpp,wrapped_functions=('i9100_i2c_samsung_init',))
e.result['emitted_packets']={f'sensor-{n}.bin':hashlib.sha256((e.out/f'sensor-{n}.bin').read_bytes()).hexdigest() for n in range(1,4)}
e.result['nested_fixtures']={name:hashlib.sha256((REPO/'port-tests'/name).read_bytes()).hexdigest() for name in
 ['i9100_sensor_worker_test.cpp','i9100_native_parent_model.h','i9100_native_i2c_model.h','i9100_i2c_register_model.h']}
if a.cross_compile:e.cross_compile(c+cpp)
e.finish(c+cpp,'Actual native I2C/controller/sensors/worker and exclusive encoded collector; physical registers/IOKit/clock are facades, no published IOHID/CoreMotion service')
