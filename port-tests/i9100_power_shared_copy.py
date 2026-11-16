"""Apply shared-power integration only to retained source copies."""
import hashlib
import json
from pathlib import Path
from i9100_offline_build import REPO
def prepare(e):
    manifest = json.loads((REPO / "port-tests/i9100_power_shared_integration.json").read_text())
    patch = REPO / "port-tests/i9100_power_shared_integration.patch"
    if hashlib.sha256(patch.read_bytes()).hexdigest() != manifest["patch_sha256"]:
        raise RuntimeError("Shared power patch hash changed")
    tree = e.out / "shared-power-copy"
    names = {}
    for path, expected in manifest["source_sha256"].items():
        source = REPO / path
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError("Live shared power source changed; regenerate: " + path)
        destination = tree / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        names[destination.name] = destination
    e.run(["git", "apply", "--check", "--directory=" + str(tree.relative_to(REPO)), str(patch)], "shared-power-apply-check", cwd=REPO)
    e.run(["git", "apply", "--directory=" + str(tree.relative_to(REPO)), str(patch)], "shared-power-copy-apply", cwd=REPO)
    for path, expected in manifest["source_sha256"].items():
        if hashlib.sha256((REPO / path).read_bytes()).hexdigest() != expected:
            raise RuntimeError("Live source changed during integration copy")
    e.result["shared_power_copy"] = {name: {"path": str(path.relative_to(REPO)), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                                      for name, path in names.items()}
    return names
