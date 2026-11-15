#!/usr/bin/env python3
"""Verify real file-change isolation during link-input freezing, no kernel build."""
import json
from datetime import datetime
from pathlib import Path
from unittest.mock import patch
from i9100_offline_build import REPO,cap_cpu
from i9100_link_snapshot import freeze,verify

cap_cpu()
out=REPO/"out"/("link-snapshot-check-"+datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
out.mkdir()
source=out/"shared-example.o"
source.write_bytes(b"original bytes")
frozen,manifest=freeze([source],out/"first")
source.write_bytes(b"new live BUILD bytes")
assert Path(frozen[0]).read_bytes()==b"original bytes"
assert verify(manifest)[0]["bytes"]==14
Path(frozen[0]).write_bytes(b"corrupt frozen bytes")
try:
    verify(manifest)
    raise AssertionError("Changed frozen object accepted")
except RuntimeError:
    pass
original_write=Path.write_bytes
def mutate(path,data):
    result=original_write(path,data)
    if path.parent==out/"raced":
        original_write(source,b"Changed during actual copy")
    return result
try:
    with patch.object(Path,"write_bytes",mutate):
        freeze([source],out/"raced")
    raise AssertionError("Concurrent input change accepted")
except RuntimeError:
    pass
assert json.loads((out/"raced/snapshot.json").read_text())["result"]=="INCOMPLETE"
(out/"result.json").write_text(json.dumps(dict(result="PASS",hardware_tested=False,
    coverage="live source changes cannot alter frozen bytes; copied-object corruption and copy-time races are rejected"),indent=2)+"\n")
print("PASS real link-input snapshot change/race checks, evidence retained",out)
