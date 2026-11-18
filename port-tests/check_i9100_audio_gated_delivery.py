#!/usr/bin/env python3
"""Audit current model/source proofs and the opt-in builder without deployment."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def retained(path):
    path = path.resolve()
    if not path.is_relative_to(REPO / "out"):
        raise RuntimeError("Proof must remain in retained XNU out")
    proof = json.loads((path / "manifest.json").read_text())
    if proof.get("result") != "PASS":
        raise RuntimeError("Incomplete proof: " + str(path))
    for name, expected in proof.get("sources", {}).items():
        if Path(name).name != name or digest(PLATFORM / name) != expected:
            raise RuntimeError("Recorded production source/header changed: " + name)
    return path, proof


def arm_sources(path):
    commands = json.loads((path / "compile-commands.json").read_text())
    if not commands:
        raise RuntimeError("ARM proof has no compiler commands")
    for item in commands:
        if "-O1" not in item["arguments"] or digest(ROOT / item["source_file"]) != item["source_sha256"]:
            raise RuntimeError("ARM source/optimization proof changed: " + item["source"])
        if item["module_header"] and digest(ROOT / item["module_header"]) != item["module_header_sha256"]:
            raise RuntimeError("ARM module header changed: " + item["module_header"])
    return len(commands)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kernel", type=Path, required=True)
    p.add_argument("--model", type=Path, action="append", required=True)
    p.add_argument("--compile-proof", type=Path, action="append", default=[],
                   help="Reuse ARM source/header proof separately from a historical model entry")
    args = p.parse_args()
    e = Evidence("audio-gated-delivery")
    e.wait_idle_seconds = 60
    models = {}
    for supplied in args.model:
        path, proof = retained(supplied)
        key = str(path.relative_to(ROOT))
        if key in models:
            raise RuntimeError("Duplicate model proof")
        if not (proof.get("models") or proof.get("native_model")):
            raise RuntimeError("--model requires an actual recorded model entry: " + key)
        for field, hash_field in (("models", "models_sha256"), ("native_model", "native_model_sha256")):
            if proof.get(field) and digest(REPO / "port-tests" / proof[field]) != proof[hash_field]:
                raise RuntimeError("Model entry changed since its recorded run: " + key)
        for field in ("nested_fixtures", "model_includes", "audio_model_fixtures_sha256"):
            for name, expected in proof.get(field, {}).items():
                if digest(REPO / "port-tests" / name) != expected:
                    raise RuntimeError("Included model fixture changed: " + name)
        if proof.get("vm_fixture_sha256") and digest(REPO / "port-tests/i9100_pl330_ring_test.c") != proof["vm_fixture_sha256"]:
            raise RuntimeError("Native ring's included instruction fixture changed")
        for name, expected in proof.get("facade_headers", {}).items():
            if digest(REPO / "port-tests/offline-native-sdk" / name) != expected:
                raise RuntimeError("Host facade header changed: " + name)
        for name, item in proof.get("pdev_sources", {}).items():
            source = REPO / item["source"] if isinstance(item, dict) else REPO / "userland/audio" / name
            expected = item["sha256"] if isinstance(item, dict) else item
            if digest(source) != expected:
                raise RuntimeError("Pdev input changed: " + name)
        models[key] = dict(manifest_sha256=digest(path / "manifest.json"),
                           source_count=len(proof["sources"]), suites=proof.get("model_suites"))
    kernel_path, kernel = retained(args.kernel)
    if not kernel.get("complete_kernel_link") or digest(kernel_path / "mach_kernel.sys") != kernel["linked_kernel_sha256"]:
        raise RuntimeError("Complete kernel bytes do not match the recorded proof")
    if kernel.get("kernel_bytes") != (kernel_path / "mach_kernel.sys").stat().st_size:
        raise RuntimeError("Recorded kernel length changed")
    count = arm_sources(kernel_path)
    compile_proofs = {}
    for supplied in args.compile_proof:
        path, proof = retained(supplied)
        if str(path.relative_to(ROOT)) in compile_proofs:
            raise RuntimeError("Duplicate historical ARM proof")
        if not proof.get("cross_compiled"):
            raise RuntimeError("Historical ARM proof is missing compiler evidence")
        compile_proofs[str(path.relative_to(ROOT))] = dict(objects=arm_sources(path),
            model_entry_reused=False, manifest_sha256=digest(path / "manifest.json"))
    groups = {}
    for node in ast.parse((REPO / "port-tests/check_i9100_peripheral_all.py").read_text()).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id in ("C_SOURCES", "CPP_SOURCES"):
            groups[node.targets[0].id] = ast.literal_eval(node.value)
    names = groups["C_SOURCES"] + groups["CPP_SOURCES"]
    if len(names) != len(set(names)) or kernel["peripheral_objects"] != len(names):
        raise RuntimeError("Kernel/inventory object count differs")
    loader = ROOT / "uniloader-S2I9100"
    source = loader / "scripts/i9100/prepare-automation.py"
    patch = loader / "scripts/i9100/peripheral-offline-objects-integration.patch"
    record = json.loads((loader / "scripts/i9100/peripheral-offline-objects-integration.json").read_text())
    expected = ["iokit/" + Path(name).stem + (".cpo" if name.endswith(".cpp") else ".o") for name in names]
    if digest(source) != record["source_sha256"] or digest(patch) != record["patch_sha256"] or record["new_objects"] != expected:
        raise RuntimeError("Loader source/patch/inventory changed")
    preview = loader / "out" / e.out.name / "source-copy"
    copied = preview / "scripts/i9100/prepare-automation.py"
    copied.parent.mkdir(parents=True)
    copied.write_bytes(source.read_bytes())
    e.run(["git", "apply", "--check", str(patch)], "loader-live-apply-check", cwd=loader)
    e.run(["git", "apply", "--directory=" + str(preview.relative_to(loader)), str(patch)],
          "loader-apply-to-copy", cwd=loader)
    found = [ast.literal_eval(node.value) for node in ast.walk(ast.parse(copied.read_text()))
             if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "peripheral_objects" for t in node.targets)]
    if found != [expected] or digest(source) != record["source_sha256"]:
        raise RuntimeError("Copied builder AST differs or live builder changed")
    e.result.update(model_proofs=models, compile_proofs=compile_proofs,
                    validated_kernel=dict(path=str(kernel_path.relative_to(ROOT)), bytes=kernel["kernel_bytes"],
                        sha256=kernel["linked_kernel_sha256"], objects=kernel["total_link_objects"],
                        peripheral_objects=len(names), compiled_current_objects=count),
                    loader_source_sha256=record["source_sha256"], loader_patch_sha256=record["patch_sha256"],
                    patched_builder_ast=str(copied.relative_to(ROOT)), live_builder_modified=False)
    e.finish([], "Current selected model entries/fixtures and production hashes; ARM source/module-header proof separately reused; kernel bytes and copied loader inventory/AST validated; no hardware/deployment")


if __name__ == "__main__":
    main()
