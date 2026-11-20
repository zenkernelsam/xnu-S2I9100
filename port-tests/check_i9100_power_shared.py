#!/usr/bin/env python3
"""Check original Power/Mali shared raw wire, retained owners and copied integration."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO, PLATFORM
from i9100_power_shared_copy import prepare
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
p.add_argument("--arm-only",action="store_true")
a=p.parse_args()
e=Evidence("power-shared-offline")
e.wait_idle_seconds=60
copies=prepare(e)
if not a.arm_only:
    e.run(["clang","-std=c99","-O1","-Wall","-Wextra","-Werror","-fno-pie","-fsanitize=address,undefined","-fno-sanitize-recover=all",
           "-I"+str(PLATFORM),"-c",str(copies["i9100_power_bus.c"]),"-o",str(e.out/"raw-wire.o")],"shared-wire-model-compile")
    e.run(["clang","-std=c99","-O1","-Wall","-Wextra","-Werror","-fno-pie","-fsanitize=address,undefined","-fno-sanitize-recover=all",
           "-I"+str(PLATFORM),"-c",str(PLATFORM/"i9100_power.c"),"-o",str(e.out/"power-policy.o")],"shared-policy-compile")
    e.run(["clang","-std=c99","-O1","-Wall","-Wextra","-Werror","-fno-pie","-fsanitize=address,undefined","-fno-sanitize-recover=all",
           "-I"+str(PLATFORM),"-c",str(PLATFORM/"i9100_radio_survey.c"),"-o",str(e.out/"radio-survey.o")],"shared-survey-compile")
    e.run(["clang++","-std=c++11","-O1","-Wall","-Wextra","-Werror","-fno-pie","-no-pie","-fsanitize=address,undefined","-fno-sanitize-recover=all",
           "-I"+str(REPO/"port-tests/offline-native-sdk"),"-I"+str(PLATFORM),str(REPO/"port-tests/i9100_power_shared_test.cpp"),
           str(PLATFORM/"i9100_power_shared.cpp"),str(e.out/"raw-wire.o"),str(e.out/"power-policy.o"),str(e.out/"radio-survey.o"),"-pthread","-o",str(e.out/"models")],"shared-model-compile")
    e.run([str(e.out/"models")],"shared-model-tests")
    for n in range(1,4):e.run([str(e.out/"models"),str(n)],"prepare-failure-"+str(n)+"-tests")
if a.cross_compile or a.arm_only:
    e.cross_compile(["i9100_power_shared.cpp","i9100_power_bus.c","IOS7I9100Power.cpp","AppleARMPE.cpp"],copies)
e.result['fixture_sources']={str(path.relative_to(REPO)):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    (REPO/'port-tests/i9100_power_shared_test.cpp',REPO/'port-tests/i9100_power_test.cpp',PLATFORM/'i9100_power.c',
     PLATFORM/'i9100_power.h',PLATFORM/'i9100_rtc_time.h')}
e.result['rtc_read_contract']='BUS0/slave06 only CONTROL03/1B or TIME10/7B reads, no writes/alarms/UPDATE'
e.result['shared_power_rtc_model']=not a.arm_only
if not a.arm_only:e.result['model_artifact_sha256']=hashlib.sha256((e.out/'models').read_bytes()).hexdigest()
e.finish(["i9100_power_shared.cpp"],"Actual shared gate/read lifetime; paired copied raw bus and ARM Power/PE; host IO/owner model, no device/rail proof")
