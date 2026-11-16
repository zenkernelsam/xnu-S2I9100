#!/usr/bin/env python3
"""Check both optional patches together on retained out copies, never the live tree."""
import ast
import hashlib
import json
from pathlib import Path
from i9100_offline_build import Evidence, REPO

e = Evidence("peripheral-patch-check")
e.wait_idle_seconds = 60
paths = ["iokit/conf/files.arm", "iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/AppleARMPE.cpp"]
tree = e.out / "integration-tree"
captured = {}
for path in paths:
    original = (REPO / path).read_bytes()
    captured[path] = hashlib.sha256(original).hexdigest()
    destination = tree / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(original)
patches = [REPO / "port-tests/i9100_emmc_integration.patch",
           REPO / "port-tests/i9100_peripheral_buses_build.patch"]
patch_hashes = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest() for path in patches}
prefix = str(tree.relative_to(REPO))
e.run(["git", "apply", "--check", "--directory=" + prefix, *map(str, patches)], "joint-apply-check", cwd=REPO)
e.run(["git", "apply", "--directory=" + prefix, *map(str, patches)], "apply-to-out-copy", cwd=REPO)
inventory = ast.parse((REPO / "port-tests/check_i9100_peripheral_all.py").read_text())
groups = {}
for node in inventory.body:
    if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
        if node.targets[0].id in ("C_SOURCES", "CPP_SOURCES"):
            groups[node.targets[0].id] = ast.literal_eval(node.value)
names = groups["C_SOURCES"] + groups["CPP_SOURCES"]
lines = (tree / paths[0]).read_text().splitlines()
for name in names:
    expected = "iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/" + name + (" optional iokitcpp" if name.endswith(".cpp") else " standard")
    if lines.count(expected) != 1:
        raise RuntimeError("Optional object missing/duplicated: " + name)
pe = (tree / paths[1]).read_text()
if pe.count("extern void ios7i9100_register_emmc_ro(IOService *platform);") != 1 or pe.count("    ios7i9100_register_emmc_ro(this);") != 1:
    raise RuntimeError("RO registration missing or duplicated")
for path, expected in captured.items():
    if hashlib.sha256((REPO / path).read_bytes()).hexdigest() != expected:
        raise RuntimeError("Live source changed during copy integration: " + path)
for path in patches:
    if hashlib.sha256(path.read_bytes()).hexdigest() != patch_hashes[str(path.relative_to(REPO))]:
        raise RuntimeError("Integration patch changed during check")
e.result.update(live_source_sha256=captured, integration_patch_sha256=patch_hashes,
                object_count=len(names), live_tree_modified=False, patched_only=str(tree.relative_to(REPO)))
e.finish([], "Both eMMC/peripheral patches composed on two out file copies; full inventory once, live files unchanged, no device")
