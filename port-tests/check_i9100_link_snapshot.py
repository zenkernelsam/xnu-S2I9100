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
deep=out/(("0636-"*40)+"IOFilterInterruptEventSource.cpo")
deep.write_bytes(b"stable object bytes")
previous=[deep]
for index in range(64):
    previous,copied=freeze(previous,out/("repeat-"+str(index)))
    assert Path(previous[0]).name=="0000-IOFilterInterruptEventSource.cpo"
    assert verify(copied)[0]["bytes"]==len(b"stable object bytes")
    assert Path(previous[0]).read_bytes()==b"stable object bytes"
duplicate0=out/"0012-duplicate.o";duplicate1=out/"duplicate.o"
duplicate0.write_bytes(b"first unique index");duplicate1.write_bytes(b"second unique index")
distinct,distinct_manifest=freeze([duplicate0,duplicate1],out/"duplicate-basenames")
assert Path(distinct[0]).name=="0000-duplicate.o" and Path(distinct[1]).name=="0001-duplicate.o"
assert verify(distinct_manifest)[0]["sha256"]!=verify(distinct_manifest)[1]["sha256"]
(out/"result.json").write_text(json.dumps(dict(result="PASS",hardware_tested=False,
    coverage="live source changes cannot alter frozen bytes; corruption/races rejected; 64 refreezes stay short with full provenance and distinct equal basenames"),indent=2)+"\n")
print("PASS real link-input snapshot change/race checks, evidence retained",out)
