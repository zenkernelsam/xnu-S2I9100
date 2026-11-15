#!/usr/bin/env python3
"""Retain immutable Linux v6.1 references under project research, without building."""
import argparse
import hashlib
import json
import os
import sys
import urllib.request
from pathlib import Path

if os.environ.get("I9100_CPU_CAPPED") != "1":
    os.execvpe("taskset", ["taskset", "-c", "0-13", "nice", "-n10", "ionice", "-c2", "-n7",
                          sys.executable, "-B", __file__, *sys.argv[1:]],
               dict(os.environ, I9100_CPU_CAPPED="1"))
root = Path(__file__).resolve().parents[2]
destination = root / "research/linux-reference-20261116"
destination.mkdir(exist_ok=True)

def request(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "i9100-offline-research"}), timeout=30) as response:
        return response.read()

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("paths", nargs="+")
a = p.parse_args()
pin_file = destination / "source-pin.json"
if pin_file.exists():
    pin = json.loads(pin_file.read_text())
else:
    commit = json.loads(request("https://api.github.com/repos/torvalds/linux/commits/v6.1"))["sha"]
    pin = dict(repository="torvalds/linux", ref="v6.1", commit=commit)
    pin_file.write_text(json.dumps(pin, indent=2) + "\n")
manifest_file = destination / "references.json"
manifest = json.loads(manifest_file.read_text()) if manifest_file.exists() else {}
for name in a.paths:
    if name.startswith("/") or ".." in Path(name).parts or "\\" in name:
        raise ValueError("Only explicit repository-relative source paths")
    path = destination / name.replace("/", "__")
    url = "https://raw.githubusercontent.com/torvalds/linux/" + pin["commit"] + "/" + name
    data = path.read_bytes() if path.exists() else request(url)
    if name in manifest and hashlib.sha256(data).hexdigest() != manifest[name]["sha256"]:
        raise RuntimeError("Retained reference changed: " + name)
    if not path.exists():
        path.write_bytes(data)
    manifest[name] = dict(pin, url=url, file=str(path.relative_to(root)), bytes=len(data),
                         sha256=hashlib.sha256(data).hexdigest(), hardware_tested=False)
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n")
    print("RETAINED", name, flush=True)
