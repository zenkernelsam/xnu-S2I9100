#!/usr/bin/env python3
"""Actual optional service companion publication and all owned sensor layers."""
import hashlib
from i9100_offline_build import Evidence, REPO, PLATFORM
c=['i9100_gpio_broker.c','i9100_clock_gates.c','i9100_irq_registry.c','i9100_peripheral_board.c',
 'i9100_i2c_samsung.c','i9100_uart_bluetooth.c','i9100_sdio.c','i9100_irq_cascade.c','i9100_sensors.c',
 'i9100_sensor_hid_wire.c','i9100_sensor_hid_queue.c']
cpp=['i9100_sensor_worker.cpp','i9100_sensor_hid.cpp','i9100_sensor_hid_endpoint.cpp']
e=Evidence('sensor-hid-service-offline');e.wait_idle_seconds=60
e.model_cpp('i9100_sensor_hid_service_test.cpp',c,cpp,wrapped_functions=('i9100_i2c_samsung_init',))
e.result['nested_fixtures']={name:hashlib.sha256((REPO/'port-tests'/name).read_bytes()).hexdigest() for name in
 ['i9100_sensor_hid_service_model.h','i9100_sensor_hid_endpoint_test.cpp','i9100_sensor_hid_test.cpp',
  'i9100_sensor_worker_test.cpp','i9100_native_parent_model.h','i9100_native_i2c_model.h','i9100_i2c_register_model.h']}
e.result['actual_companion_sha256']=hashlib.sha256((PLATFORM/'IOS6I9100SensorHIDService.inc').read_bytes()).hexdigest()
e.finish(c+cpp,'Actual service companion path and all downstream driver layers; property/registry/IOKit/physical registers/timebase facades, no real HID/CoreMotion discovery')
