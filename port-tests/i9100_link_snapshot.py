#!/usr/bin/env python3
"""Freeze retained link inputs under the caller's out, never edit shared BUILD."""
import hashlib
import json
from pathlib import Path
from i9100_offline_build import REPO,ROOT

def sha(data):
    return hashlib.sha256(data).hexdigest()

def freeze(paths,output):
    output=Path(output).resolve()
    if not output.is_relative_to((REPO/"out").resolve()) or output.exists():
        raise RuntimeError("Frozen inputs need a new project out folder")
    output.mkdir(parents=True)
    records=[]
    frozen=[]
    try:
        for index,original in enumerate(paths):
            source=Path(original).resolve()
            if not source.is_relative_to(ROOT.resolve()) or not source.is_file():
                raise RuntimeError("Link input is missing or outside the project: "+str(source))
            data=source.read_bytes()
            target=output/(str(index).zfill(4)+"-"+source.name)
            target.write_bytes(data)
            record=dict(source=str(source.relative_to(ROOT.resolve())),frozen=str(target.relative_to(ROOT.resolve())),
                        bytes=len(data),sha256=sha(data))
            records.append(record)
            if sha(source.read_bytes())!=record["sha256"] or sha(target.read_bytes())!=record["sha256"]:
                raise RuntimeError("Link input changed while freezing: "+str(source))
            frozen.append(str(target))
    except Exception as error:
        (output/"snapshot.json").write_text(json.dumps(dict(result="INCOMPLETE",error=str(error),inputs=records),indent=2)+"\n")
        raise
    manifest=output/"snapshot.json"
    manifest.write_text(json.dumps(dict(result="PASS",inputs=records,
        semantics="Actual input bytes captured now; this does not reconstruct the historical reference binary"),indent=2)+"\n")
    return frozen,manifest

def verify(manifest):
    data=json.loads(Path(manifest).read_text())
    if data.get("result")!="PASS":
        raise RuntimeError("Incomplete link-input snapshot")
    for item in data["inputs"]:
        path=(ROOT/item["frozen"]).resolve()
        if not path.is_relative_to((REPO/"out").resolve()):
            raise RuntimeError("Frozen input escaped project out")
        content=path.read_bytes()
        if len(content)!=item["bytes"] or sha(content)!=item["sha256"]:
            raise RuntimeError("Frozen link input changed: "+item["frozen"])
    return data["inputs"]
