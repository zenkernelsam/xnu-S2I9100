#!/usr/bin/env python3
"""Audit retained camera models, ARM objects, complete freeze and unapplied integration."""
import argparse
import ast
import hashlib
import json
import re
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM
from i9100_link_snapshot import verify


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def retained(folder):
    folder = folder.resolve()
    if not folder.is_relative_to(REPO / "out"):
        raise RuntimeError("Evidence escaped retained XNU out")
    manifest = json.loads((folder / "manifest.json").read_text())
    if manifest.get("result") != "PASS" or manifest.get("hardware_tested") is not False:
        raise RuntimeError("Require actual successful offline evidence")
    for name, expected in manifest.get("sources", {}).items():
        if sha(PLATFORM / name) != expected:
            raise RuntimeError("Evidence source drift: " + name)
    if manifest.get("cross_compiled"):
        for item in json.loads((folder / "compile-commands.json").read_text()):
            source = ROOT / item["source_file"]
            if sha(source) != item["source_sha256"]:
                raise RuntimeError("ARM source changed")
            if item["module_header"] and sha(ROOT / item["module_header"]) != item["module_header_sha256"]:
                raise RuntimeError("ARM module header changed")
            if not (ROOT / item["object"]).is_file():
                raise RuntimeError("ARM object missing")
    return folder, manifest


def object_name(path):
    return re.sub(r"^(?:[0-9]{4}-)+", "", Path(path).name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", action="append", type=Path, required=True)
    p.add_argument("--kernel", type=Path, required=True)
    p.add_argument("--references", type=Path, required=True)
    p.add_argument("--patch-check", type=Path, required=True)
    a = p.parse_args()
    e = Evidence("camera-delivery")
    e.wait_idle_seconds = 60
    models = {}
    for supplied in a.model:
        folder, m = retained(supplied)
        found = False
        for key, hash_key in (("models", "models_sha256"), ("native_model", "native_model_sha256")):
            if m.get(key):
                found = True
                if sha(REPO / "port-tests" / m[key]) != m[hash_key]:
                    raise RuntimeError("Model code changed")
        if not found:
            raise RuntimeError("Not a model proof")
        for name, expected in m.get("facade_headers", {}).items():
            if sha(REPO / "port-tests/offline-native-sdk" / name) != expected:
                raise RuntimeError("Host facade changed")
        models[str(folder.relative_to(ROOT))] = sha(folder / "manifest.json")
    kernel_folder, kernel = retained(a.kernel)
    if not kernel.get("complete_kernel_link") or sha(kernel_folder / "mach_kernel.sys") != kernel["linked_kernel_sha256"]:
        raise RuntimeError("Complete kernel changed")
    for name in kernel["frozen_input_manifests"]:
        verify(ROOT / name)
    inputs = (kernel_folder / "link.filelist").read_text().splitlines()
    groups = {}
    for node in ast.parse((REPO / "port-tests/check_i9100_peripheral_all.py").read_text()).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id in ("C_SOURCES", "CPP_SOURCES"):
            groups[node.targets[0].id] = ast.literal_eval(node.value)
    names = groups["C_SOURCES"] + groups["CPP_SOURCES"]
    if len(names) != len(set(names)) or kernel["peripheral_objects"] != len(names) or kernel["total_link_objects"] != len(inputs):
        raise RuntimeError("Full peripheral inventory mismatch")
    for name in names:
        kind = ".cpo" if name.endswith(".cpp") else ".o"
        suffixes = (name + kind, Path(name).stem + kind)
        if sum(object_name(path) in suffixes for path in inputs) != 1:
            raise RuntimeError("Complete link object missing or duplicate: " + name)
    # Match every delivered camera byte to a current source compile, then
    # compare that exact object with the final frozen input. Old PASS labels
    # cannot silently substitute for a newly edited module.
    compile_items = []
    cursor = kernel_folder
    seen = set()
    needed = {"i9100_camera_engine_io.c", "i9100_csis.c", "i9100_fimc.c", "i9100_camera_native.cpp", "i9100_camera_mmio.cpp"}
    for _ in range(32):
        if cursor in seen:
            raise RuntimeError("Cyclic complete reference")
        seen.add(cursor)
        compile_items += json.loads((cursor / "compile-commands.json").read_text())
        if needed.issubset({x["source"] for x in compile_items}):
            break
        record = json.loads((cursor / "manifest.json").read_text())
        if not record.get("base"):
            break
        cursor = (ROOT / record["base"]).resolve()
        if not cursor.is_relative_to(REPO / "out"):
            raise RuntimeError("Full reference escaped project out")
    else:
        raise RuntimeError("Incomplete reference ancestry")
    for name in sorted(needed):
        candidates = [x for x in compile_items if x["source"] == name]
        if not candidates:
            raise RuntimeError("Camera compile proof missing")
        chosen = candidates[0]
        if sha(PLATFORM / name) != chosen["source_sha256"]:
            raise RuntimeError("Current camera code was not linked: " + name)
        if chosen["module_header"] and sha(ROOT / chosen["module_header"]) != chosen["module_header_sha256"]:
            raise RuntimeError("Current camera header was not compiled")
        kind = ".cpo" if name.endswith(".cpp") else ".o"
        matching = [path for path in inputs if object_name(path) in (name + kind, Path(name).stem + kind)]
        if len(matching) != 1 or sha(matching[0]) != sha(ROOT / chosen["object"]):
            raise RuntimeError("Frozen camera input differs from actual compiled object")
    refs = a.references.resolve()
    if not refs.is_relative_to(ROOT / "research"):
        raise RuntimeError("Reference evidence escaped research")
    r = json.loads((refs / "source-facts.json").read_text())
    if r.get("result") != "PASS" or r.get("hardware_tested") is not False or r.get("source_reference_only") is not True:
        raise RuntimeError("Source facts cannot stand in for hardware proof")
    for _, item in r["sources"].items():
        if sha(ROOT / item["path"]) != item["sha256"]:
            raise RuntimeError("Pinned source changed")
    loader = ROOT / "uniloader-S2I9100"
    reference_tool = loader / "scripts/i9100/research-camera-capture.py"
    if sha(reference_tool) != r["verifier_sha256"]:
        raise RuntimeError("Reference verifier changed")
    patch_folder, joint = retained(a.patch_check)
    for name, expected in joint["live_source_sha256"].items():
        if sha(REPO / name) != expected:
            raise RuntimeError("Live platform changed after patch check")
    for name, expected in joint["integration_patch_sha256"].items():
        if sha(REPO / name) != expected:
            raise RuntimeError("Unapplied XNU patch drift")
    if joint["object_count"] != len(names) or joint["live_tree_modified"]:
        raise RuntimeError("Bad joint-copy integration")
    source = loader / "scripts/i9100/prepare-automation.py"
    patch = loader / "scripts/i9100/peripheral-offline-objects-integration.patch"
    record = json.loads(patch.with_suffix(".json").read_text())
    objects = ["iokit/" + Path(n).stem + (".cpo" if n.endswith(".cpp") else ".o") for n in names]
    if sha(source) != record["source_sha256"] or sha(patch) != record["patch_sha256"] or record["new_objects"] != objects or record["applied"]:
        raise RuntimeError("Current loader patch is stale or applied")
    tree = loader / "out" / e.out.name / "builder-copy"
    target = tree / "scripts/i9100/prepare-automation.py"
    target.parent.mkdir(parents=True)
    target.write_bytes(source.read_bytes())
    e.run(["git", "apply", "--check", str(patch)], "loader-live-apply-check", cwd=loader)
    e.run(["git", "apply", "--directory=" + str(tree.relative_to(loader)), str(patch)], "loader-copy-apply", cwd=loader)
    lists = [ast.literal_eval(n.value) for n in ast.walk(ast.parse(target.read_text())) if isinstance(n, ast.Assign)
             and any(isinstance(t, ast.Name) and t.id == "peripheral_objects" for t in n.targets)]
    if lists != [objects] or sha(source) != record["source_sha256"]:
        raise RuntimeError("Patched builder inventory mismatch or live drift")
    e.result.update(model_proofs=models, references_sha256=sha(refs / "source-facts.json"),
                    validated_kernel=dict(path=str(kernel_folder.relative_to(ROOT)), bytes=kernel["kernel_bytes"],
                                          sha256=kernel["linked_kernel_sha256"], objects=len(inputs), peripherals=len(names)),
                    joint_patch_proof_sha256=sha(patch_folder / "manifest.json"),
                    loader_source_sha256=record["source_sha256"], loader_patch_sha256=record["patch_sha256"],
                    live_builder_modified=False)
    e.finish([], "Current camera cores/native/ARM objects, frozen full kernel, pinned source facts and out-only integration; no hardware or loader deployment")


if __name__ == "__main__":
    main()
