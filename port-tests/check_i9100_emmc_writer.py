#!/usr/bin/env python3
"""Exercise actual MSHCI writer policy with explicit historical/synthetic media evidence."""
import argparse
import hashlib
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
e = Evidence("emmc-writer-offline")
e.wait_idle_seconds = 60
source = ROOT / "assets/i9100-backup/mmcblk0-head1mb.img"
expected = "879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef"
if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
    raise RuntimeError("Historical primary GPT/PIT changed")
sources = ["i9100_emmc.c", "i9100_emmc_layout.c", "i9100_emmc_writer.c"]
e.run(["clang", "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror", "-fno-pie", "-no-pie",
       "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + str(PLATFORM),
       str(REPO / "port-tests/i9100_emmc_writer_test.c"), *[str(PLATFORM / n) for n in sources],
       "-o", str(e.out / "models")], "compile-models")
e.run([str(e.out / "models"), str(source)], "writer-model-tests")
e.result.update(models="i9100_emmc_writer_test.c",
                models_sha256=hashlib.sha256((REPO / "port-tests/i9100_emmc_writer_test.c").read_bytes()).hexdigest(),
                nested_fixtures={"i9100_emmc_test.c": hashlib.sha256((REPO / "port-tests/i9100_emmc_test.c").read_bytes()).hexdigest()},
                historical_primary_gpt_sha256=expected, backup_gpt_model_fixture=True)
if a.cross_compile:
    e.cross_compile(sources)
e.finish(sources, "Actual lower MSHCI write/flush and CID/current-table/window policy with explicit FIFO/backup GPT/media facades; no device contact, mount or flash")
