"""Prepare exact native-emmc BSD alias/root patch on retained out copies only."""
import hashlib
import json
from i9100_offline_build import REPO, PLATFORM


def prepare(e):
    meta = json.loads((REPO / "port-tests/i9100_emmc_storage_integration.json").read_text())
    patch = REPO / "port-tests/i9100_emmc_storage_integration.patch"
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(patch) != meta["patch_sha256"] or any(sha(REPO / path) != expected for path, expected in meta["sources"].items()):
        raise RuntimeError("Live storage/root patch source drift; regenerate unapplied integration")
    tree = e.out / "emmc-service-copy"
    copied = {}
    for name in meta["sources"]:
        target = tree / name
        target.parent.mkdir(parents=True)
        target.write_bytes((REPO / name).read_bytes())
        copied[target.name] = target
    e.run(["git", "apply", "--check", "--directory=" + str(tree.relative_to(REPO)), str(patch)], "emmc-service-copy-check", cwd=REPO)
    e.run(["git", "apply", "--directory=" + str(tree.relative_to(REPO)), str(patch)], "emmc-service-copy-apply", cwd=REPO)
    e.result["emmc_service_copy"] = dict(live_sources=meta["sources"], patch_sha256=meta["patch_sha256"],
        copied_sources={str(path.relative_to(REPO)): sha(path) for path in copied.values()}, live_modified=False)
    return copied


def verify(e):
    proof = e.result["emmc_service_copy"]
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if any(sha(REPO / path) != expected for path, expected in proof["live_sources"].items()) or any(
            sha(REPO / path) != expected for path, expected in proof["copied_sources"].items()):
        raise RuntimeError("Native emmc copied/current source changed")
