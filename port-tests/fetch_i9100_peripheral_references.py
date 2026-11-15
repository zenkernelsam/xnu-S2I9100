#!/usr/bin/env python3
"""Fetch pinned U1 hardware references into research, never into the build tree."""
import argparse
import hashlib
import json
import os
import sys
import urllib.request
from pathlib import Path

if os.environ.get("I9100_CPU_CAPPED") != "1":
    os.execvpe("taskset", ["taskset", "-c", "0-13", "nice", "-n10", "ionice",
                          "-c2", "-n7", sys.executable, "-B", __file__,
                          *sys.argv[1:]], dict(os.environ, I9100_CPU_CAPPED="1"))

REPOSITORY = "CyanogenMod/android_kernel_samsung_smdk4210"
ROOT = Path(__file__).resolve().parents[2]
DESTINATION = ROOT / "research/peripheral-reference-20261115"


def request(url):
    req = urllib.request.Request(url, headers={"User-Agent": "i9100-offline-research"})
    with urllib.request.urlopen(req, timeout=30) as response:
        return response.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list relevant paths only")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args()
    pin_file = DESTINATION / "source-pin.json"
    DESTINATION.mkdir(exist_ok=True)
    if pin_file.exists():
        pin = json.loads(pin_file.read_text())
    else:
        commit = json.loads(request("https://api.github.com/repos/" + REPOSITORY
                                    + "/commits/cm-10.1"))["sha"]
        pin = {"repository": REPOSITORY, "branch": "cm-10.1", "commit": commit}
        pin_file.write_text(json.dumps(pin, indent=2) + "\n")
    tree_file = DESTINATION / "source-tree.json"
    if tree_file.exists():
        tree = json.loads(tree_file.read_text())
    else:
        tree = json.loads(request("https://api.github.com/repos/" + REPOSITORY
                                 + "/git/trees/" + pin["commit"] + "?recursive=1"))
        if tree.get("truncated"):
            raise RuntimeError("Reference tree truncated; no partial inventory accepted")
        tree_file.write_text(json.dumps(tree, separators=(",", ":")) + "\n")
    blobs = {entry["path"]: entry for entry in tree["tree"] if entry["type"] == "blob"}
    if args.list:
        names = ("mc1n2", "pl330", "k3dh", "k3g", "ak8975", "haptic",
                 "gpio-exynos4", "gpio-samsung", "regs-audss", "i2c-s3c",
                 "regs-iic", "sipc4", "modem_link_device", "m5mo", "s5k5ba",
                 "s5p-mfc", "bcmsdh", "bcm4330", "dhd_sdio", "bcm203x")
        for path in sorted(blobs):
            if any(name in path for name in names):
                print(path)
        return
    manifest_file = DESTINATION / "additional-references.json"
    manifest = json.loads(manifest_file.read_text()) if manifest_file.exists() else []
    by_path = {entry["path"]: entry for entry in manifest}
    for path in args.paths:
        if path not in blobs:
            raise RuntimeError("Missing pinned source: " + path)
        output = DESTINATION / path.replace("/", "__")
        if output.exists():
            data = output.read_bytes()
        else:
            data = request("https://raw.githubusercontent.com/" + REPOSITORY + "/"
                           + pin["commit"] + "/" + path)
        actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if actual != blobs[path]["sha"]:
            raise RuntimeError("Pinned blob mismatch: " + path)
        if not output.exists():
            output.write_bytes(data)
        by_path[path] = dict(pin, path=path, git_blob=actual,
                             sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                             file=str(output.relative_to(ROOT)),
                             evidence="source_reference_not_live_device")
        print("VERIFIED", path, flush=True)
        manifest_file.write_text(json.dumps(list(by_path.values()), indent=2) + "\n")


if __name__ == "__main__":
    main()
