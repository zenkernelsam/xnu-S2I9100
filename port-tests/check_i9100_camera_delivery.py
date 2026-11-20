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
    for name,expected in manifest.get('companion_headers',{}).items():
        if sha(PLATFORM/name)!=expected:raise RuntimeError('Compiled companion ABI changed: '+name)
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
    p.add_argument("--firmware-check", type=Path, help="Audit default-unreviewed MFC preparation against actual core output")
    p.add_argument("--include-emmc", action="store_true", help="Also bind the lower writer and paired RO device objects to current source")
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
        if m.get('extracted_production') or m.get('storage_shutdown_protocol_model'):
            found=True
            for name,expected in m['fixture_sources'].items():
                if sha(REPO/name)!=expected:raise RuntimeError('Actual VFS/USB fixture drift')
            if sha(folder/'models')!=m['model_artifact_sha256']:raise RuntimeError('VFS/USB model binary changed')
            if m.get('extracted_production') and sha(folder/'actual-vfs-model.c')!=m['extracted_production']['source_sha256']:
                raise RuntimeError('Actual extracted VFS model changed')
            proof=m['hfs_boot_copy']
            if sha(REPO/'port-tests/i9100_hfs_boot_integration.patch')!=proof['patch_sha256']:raise RuntimeError('Model HFS patch drift')
            for name,expected in dict(proof['live_sources'],**proof['companion_sources'],**proof['copied_sources']).items():
                if sha(REPO/name)!=expected:raise RuntimeError('Model original/copy HFS source drift')
        if m.get('shared_power_rtc_model'):
            found=True
            for name,expected in m['fixture_sources'].items():
                if sha(REPO/name)!=expected:raise RuntimeError('RTC/shared-power fixture drift')
            if sha(folder/'models')!=m['model_artifact_sha256']:raise RuntimeError('RTC/shared-power model changed')
        if not found:
            raise RuntimeError("Not a model proof")
        if m.get('native_model_source_override'):
            override=m['native_model_source_override']
            if sha(ROOT/override['path'])!=override['sha256']:raise RuntimeError('Retained model source override changed')
        if m.get('native_model_artifact_sha256') and sha(ROOT/m['native_model_artifact'])!=m['native_model_artifact_sha256']:
            raise RuntimeError('Retained native model binary changed')
        if m.get('model_artifact_sha256')and sha(folder/'models')!=m['model_artifact_sha256']:
            raise RuntimeError('Retained model executable changed')
        if m.get('clock_source_references'):
            for name,item in m['clock_source_references'].items():
                reference=(ROOT/name).resolve()
                if not reference.is_relative_to(ROOT/'research')or sha(reference)!=item['sha256']:
                    raise RuntimeError('Original eMMC clock reference changed')
                if item.get('git_blob'):
                    raw=reference.read_bytes()
                    blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
                    if blob!=item['git_blob']:raise RuntimeError('Original Samsung clock blob changed')
            if sha(ROOT/'uniloader-S2I9100/scripts/i9100/emmc-clock-evidence.py')!=m['python_decoder_sha256']:
                raise RuntimeError('Clock decoder changed after C cross-reference')
            for name,expected in m['record_outputs'].items():
                if sha(folder/name)!=expected:raise RuntimeError('Actual C clock record changed')
            original=m['original_samsung_pll_crossref']
            if sha(ROOT/original['source'])!=original['sha256']or sha(folder/'original-pll-model')!=original['model_sha256']:
                raise RuntimeError('Original Samsung PLL arithmetic model changed')
            if sha(folder/'models')!=m['model_artifact_sha256']:raise RuntimeError('Clock model executable changed')
        for name,item in m.get('sdr_reference_sources',{}).items():
            reference=(ROOT/name).resolve()
            if not reference.is_relative_to(ROOT/'research')or sha(reference)!=item['sha256']:
                raise RuntimeError('SDR original board/controller source changed')
            raw=reference.read_bytes()
            if hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()!=item['git_blob']:
                raise RuntimeError('SDR original reference Gitblob changed')
        if m.get('actual_matching'):
            proof=m['actual_matching']
            for name,expected in proof['source_files'].items():
                if sha(REPO/name)!=expected:raise RuntimeError('Actual XNU matching source drift')
            if sha(ROOT/proof['model_source'])!=proof['model_source_sha256'] or sha(REPO/'port-tests/i9100_emmc_matching_test.cpp')!=proof['fixture_sha256']:
                raise RuntimeError('Actual matching fixture drift')
            if sha(PLATFORM/'i9100_emmc_publication.cpp')!=proof['personality_source_sha256'] or sha((ROOT/proof['model_source']).parent/'actual-personalities.inc')!=proof['personality_fixture_sha256']:
                raise RuntimeError('Actual matching personality source/fixture drift')
        for name, expected in m.get("facade_headers", {}).items():
            if sha(REPO / "port-tests/offline-native-sdk" / name) != expected:
                raise RuntimeError("Host facade changed")
        for name, expected in m.get("extra_facade_headers", {}).items():
            if sha(REPO / name) != expected:
                raise RuntimeError("Extra host allocation/PTE facade changed")
        for name, expected in m.get("nested_fixtures", {}).items():
            if sha(REPO / "port-tests" / name) != expected:
                raise RuntimeError("Nested model fixture changed")
        if m.get('sdr_boot_path'):
            if sha(folder/'actual-contract-sdr.bin')!=m['actual_sdr_contract_sha256']or sha(PLATFORM/'i9100_emmc_sdr.h')!=m['sdr_companion_header_sha256']:
                raise RuntimeError('Actual SDR boot wire or consumed ABI changed')
        if m.get("host_source_override"):
            override = m["host_source_override"]
            if sha(PLATFORM / "i9100_dma_native.cpp") != override["original_sha256"] or sha(folder / "i9100_dma_native.cpp") != override["copy_sha256"]:
                raise RuntimeError("Actual DMA source/host-only CPU barrier substitution drift")
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
    needed = {name for name in names if name.startswith(("i9100_camera_", "i9100_csis.", "i9100_fimc.", "i9100_mfc"))
              and name != "i9100_camera_protocol.c"}
    if "i9100_mfc_native.cpp" in names:
        needed.add("i9100_dma_native.cpp")
    if a.include_emmc:
        needed.update(("i9100_emmc.c", "i9100_emmc_writer.c", "IOS7I9100EMMC.cpp", "IOS7I9100GPT.cpp"))
        needed.update(name for name in names if name.startswith(("i9100_emmc_","IOS6I9100EMMC")))
        if (PLATFORM/'i9100_emmc_sdr.h').is_file():needed.add('IOS7I9100GPT.cpp')
    if any(json.loads((ROOT/name/'manifest.json').read_text()).get('shared_power_rtc_model') for name in models):
        needed.add('i9100_power_shared.cpp')
    if kernel.get('hfs_boot_copy'):
        needed.update(Path(n).name for n in kernel['hfs_boot_copy']['copied_sources'])
    if kernel.get('emmc_boot_pe_copy'):needed.add('AppleARMPE.cpp')
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
        actual_source=ROOT/chosen['source_file'] if name in {'vfs_syscalls.c','hfs_vfsops.c','bsd_init.c','kern_shutdown.c','i9100_usbcdc.c','AppleARMPE.cpp'} else PLATFORM/name
        if sha(actual_source) != chosen["source_sha256"]:
            raise RuntimeError("Current camera code was not linked: " + name)
        if chosen["module_header"] and sha(ROOT / chosen["module_header"]) != chosen["module_header_sha256"]:
            raise RuntimeError("Current camera header was not compiled")
        if name in ('i9100_emmc.c','i9100_emmc_native.cpp'):
            dep=Path(ROOT/chosen['object']).with_suffix('.d')
            if not dep.is_file()or 'i9100_emmc_sdr.h'not in dep.read_text():raise RuntimeError('SDR ABI was not consumed by paired ARM compile')
        kind = ".cpo" if name.endswith(".cpp") else ".o"
        matching = [path for path in inputs if object_name(path) in (name + kind, Path(name).stem + kind)]
        if len(matching) != 1 or sha(matching[0]) != sha(ROOT / chosen["object"]):
            raise RuntimeError("Frozen camera input differs from actual compiled object")
    for key,patch_name in (('emmc_service_copy','i9100_emmc_storage_integration.patch'),('hfs_boot_copy','i9100_hfs_boot_integration.patch')):
        if not kernel.get(key):continue
        service=kernel[key]
        for name,expected in service["live_sources"].items():
            if sha(REPO/name)!=expected:raise RuntimeError("Current BSD alias/root integration source changed")
        patch=REPO/'port-tests'/patch_name
        if sha(patch)!=service["patch_sha256"]:raise RuntimeError("BSD alias/root patch changed")
        for name,expected in service["copied_sources"].items():
            if sha(REPO/name)!=expected:raise RuntimeError("Compiled alias/root source copy changed")
            source_name=Path(name).name
            matches=[item for item in compile_items if item["source"]==source_name]
            if not matches or matches[0]["source_sha256"]!=expected:raise RuntimeError("Patched BSD source was not actually compiled")
            kind='.cpo' if source_name.endswith('.cpp') else '.o'
            suffixes=(source_name+kind,Path(source_name).stem+kind)
            matching=[path for path in inputs if object_name(path) in suffixes]
            if len(matching)!=1 or sha(matching[0])!=sha(ROOT/matches[0]["object"]):raise RuntimeError("Patched BSD object missing from actual final freeze")
        for name,expected in service.get('companion_sources',{}).items():
            if sha(REPO/name)!=expected:raise RuntimeError('HFS boot companion changed')
    if kernel.get('emmc_boot_pe_copy'):
        pe=kernel['emmc_boot_pe_copy']
        for name,expected in dict(pe['live_sources'],**pe['patches']).items():
            if sha(REPO/name)!=expected:raise RuntimeError('Current PE boot integration changed')
        if pe.get('shared_pe_original'):
            text=(REPO/pe['shared_pe_original']).read_text();start=text.index('--- a/iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/AppleARMPE.cpp\n')
            selected=text[start:];end=selected.find('\n--- a/',1)
            if end>=0:selected=selected[:end+1]
            if hashlib.sha256(selected.encode()).hexdigest()!=pe['shared_pe_fragment_sha256']:raise RuntimeError('Shared PE hook changed')
        if sha(REPO/pe['copied_source'])!=pe['copied_source_sha256']:raise RuntimeError('Copied PE boot source changed')
        candidates=[x for x in compile_items if x['source']=='AppleARMPE.cpp']
        if not candidates or candidates[0]['source_sha256']!=pe['copied_source_sha256']:raise RuntimeError('Internal-root PE was not compiled')
        actual=[path for path in inputs if object_name(path) in ('AppleARMPE.cpo','AppleARMPE.cpp.cpo')]
        if len(actual)!=1 or sha(actual[0])!=sha(ROOT/candidates[0]['object']):raise RuntimeError('Internal-root PE object not frozen in final kernel')
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
    firmware_check = None
    if a.firmware_check:
        checked = a.firmware_check.resolve()
        if not checked.is_relative_to(loader / "out"):
            raise RuntimeError("Firmware fixture evidence escaped loader out")
        check = json.loads((checked / "test-result.json").read_text())
        if check.get("result") != "PASS" or check.get("hardware_tested") is not False or check.get("device_contacted") is not False:
            raise RuntimeError("Bad firmware preparation fixture")
        for name, key in (("prepare-mfc-firmware.py", "preparer_sha256"), ("check-mfc-firmware.py", "checker_sha256")):
            if sha(loader / "scripts/i9100" / name) != check[key]:
                raise RuntimeError("Firmware tool changed")
        proof_path, proof = retained(ROOT / check["core_model"])
        if sha(proof_path / "manifest.json") != check["core_manifest_sha256"] or sha(proof_path / "mfc-core.json") != check["core_output_sha256"]:
            raise RuntimeError("Actual C-core producer drift")
        if sha(REPO / "port-tests" / proof["models"]) != proof["models_sha256"]:
            raise RuntimeError("Actual C-core output fixture changed")
        prepared = (ROOT / check["prepared"]).resolve()
        if not prepared.is_relative_to(loader / "out"):
            raise RuntimeError("Prepared firmware escaped loader out")
        fm = json.loads((prepared / "manifest.json").read_text())
        if not fm["model_fixture"] or fm["board_data_reviewed"] or fm["profile"]["board_data_reviewed"] or fm["hardware_tested"] or fm["device_contacted"]:
            raise RuntimeError("Fixture became production firmware evidence")
        if sha(prepared / "mfc_fw.bin") != fm["profile"]["sha256"] or fm["header_plan"] != json.loads((proof_path / "mfc-core.json").read_text())["plan"]:
            raise RuntimeError("Prepared firmware/SHA/C layout mismatch")
        firmware_check = dict(path=str(checked.relative_to(ROOT)), sha256=sha(checked / "test-result.json"))
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
                    firmware_preparation_fixture=firmware_check, live_builder_modified=False)
    e.finish([], "Current camera cores/native/ARM objects, frozen full kernel, pinned source facts and out-only integration; no hardware or loader deployment")


if __name__ == "__main__":
    main()
