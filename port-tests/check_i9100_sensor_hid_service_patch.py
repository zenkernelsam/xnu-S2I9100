#!/usr/bin/env python3
"""Compile the exact optional HID class extension on a retained source copy."""
import argparse
import hashlib
import json
from i9100_offline_build import Evidence, REPO, PLATFORM
from i9100_sensor_hid_service_copy import prepare,verify
p=argparse.ArgumentParser(description=__doc__)
a=p.parse_args()
e=Evidence('sensor-hid-service-patch');e.wait_idle_seconds=60
copies=prepare(e)
e.cross_compile(['IOS7LeoHIDTouch.cpp'],copies)
verify(e)
e.finish(['i9100_sensor_hid.cpp','i9100_sensor_hid_endpoint.cpp'],
 'Exact existing IOHIDEventService class plus explicit default-OFF sensor publication compiled with actual ARM/XNU headers; live source untouched, no runtime discovery or hardware proof')
