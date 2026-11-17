#!/usr/bin/env python3
"""Extend the paired Power patch on retained copies; never edit live sources."""
import difflib
import hashlib
import json
from i9100_offline_build import Evidence, REPO
from i9100_power_shared_copy import prepare

e = Evidence("power-haptic-patch-prepare")
e.wait_idle_seconds = 60
copies = prepare(e)
raw = copies["i9100_power_bus.c"]
text = raw.read_text()
if "i9100_power_bus_filter(&shared" not in text:
    anchor = " if(!i9100_power_bus_enter(fuel,&shared))return 0;\n"
    if text.count(anchor) != 1:
        raise RuntimeError("Shared raw entry changed; inspect before generating")
    text = text.replace(anchor, anchor + " if(!i9100_power_bus_filter(&shared,slave,reg,data,count,writing)){(void)i9100_power_bus_leave(&shared);return 0;}\n", 1)
if "int i9100_power_haptic_config_read(" not in text:
    anchor = "int i9100_power_pmic_write(void *hw,unsigned reg,uint8_t value)\n{return transfer(hw,0,0x66,reg,&value,1,1);}\n"
    if text.count(anchor) != 1:
        raise RuntimeError("PMIC wrapper changed; inspect before generating")
    added = ("int i9100_power_haptic_config_read(void *hw,uint8_t *value)\n"
             "{uint8_t data=0;if(!value||!transfer(hw,0,0x48,2,&data,1,0))return 0;*value=data;return 1;}\n"
             "int i9100_power_haptic_config_write(void *hw,uint8_t value)\n"
             "{return transfer(hw,0,0x48,2,&value,1,1);}\n")
    text = text.replace(anchor, anchor + added, 1)
raw.write_text(text)
power = copies["IOS7I9100Power.cpp"]
text = power.read_text()
if "i9100_power_shared_publish_haptic" not in text:
    anchor = " return !i9100_power_shared_enabled()||!i9100_power_shared_publish(this,&hw);\n"
    if text.count(anchor) != 1:
        raise RuntimeError("Power publisher changed; inspect before generating")
    text = text.replace(anchor, " const i9100_power_haptic_ops haptic={i9100_power_haptic_config_read,i9100_power_haptic_config_write,1};\n"
                              " return !i9100_power_shared_enabled()||!i9100_power_shared_publish_haptic(this,&hw,&haptic);\n", 1)
power.write_text(text)
manifest_file = REPO / "port-tests/i9100_power_shared_integration.json"
manifest = json.loads(manifest_file.read_text())
parts = []
check = e.out / "new-patch-check"
for name, sha in manifest["source_sha256"].items():
    original = (REPO / name).read_bytes()
    if hashlib.sha256(original).hexdigest() != sha:
        raise RuntimeError("Live source changed during preparation: " + name)
    before = original.decode().replace("\r\n", "\n")
    after = copies[name.rsplit("/", 1)[-1]].read_text()
    parts.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                    fromfile="a/" + name, tofile="b/" + name))
    dest = check / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(original)
patch = REPO / "port-tests/i9100_power_shared_integration.patch"
patch.write_text("".join(parts))
manifest["patch_sha256"] = hashlib.sha256(patch.read_bytes()).hexdigest()
manifest["haptic_wire"] = "Fixed slave48 CONFIG2 + PMIC LDO14; shared filter before GPIO/IRQ-disabled IO"
manifest_file.write_text(json.dumps(manifest, indent=2) + "\n")
e.run(["git", "apply", "--check", "--directory=" + str(check.relative_to(REPO)), str(patch)],
      "new-patch-apply-check", cwd=REPO)
e.result["patch_sha256"] = manifest["patch_sha256"]
e.finish([], "Prepared paired haptic resource guard/Power callbacks only on retained out copies; no live application")
