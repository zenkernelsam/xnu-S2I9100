#!/usr/bin/env python3
"""Retained, CPU-capped offline build evidence, without changing shared BUILD."""
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ROOT = REPO.parent
PLATFORM = REPO / "iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform"


def cap_cpu():
    if os.environ.get("I9100_CPU_CAPPED") != "1":
        os.execvpe("taskset", ["taskset", "-c", "0-13", "nice", "-n10", "ionice",
                              "-c2", "-n7", sys.executable, "-B", *sys.argv],
                   dict(os.environ, I9100_CPU_CAPPED="1"))
    if set(os.sched_getaffinity(0)) - set(range(14)) or os.getpriority(os.PRIO_PROCESS, 0) < 10:
        raise RuntimeError("CPU envelope missing; refusing compilation")


def idle():
    jobs = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        try:
            name = (entry / "comm").read_text().strip()
            args = (entry / "cmdline").read_bytes().replace(b"\0", b" ").decode("utf-8", "replace")
        except OSError:
            continue
        if (name.startswith(("clang", "gcc", "cc1", "ld64", "make", "ninja", "qemu"))
                or name.startswith("python") and any(x in args for x in (
                    "prepare-automation.py", "rebuild-i9100.py", "package-oneshot.py",
                    "check_i9100_"))):
            jobs.append((entry.name, name))
    if jobs:
        raise RuntimeError("Other heavy job active; this offline build deferred: " + repr(jobs))


class Evidence:
    def __init__(self, prefix):
        cap_cpu()
        self.out = REPO / "out" / (prefix + "-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
        self.out.mkdir()
        self.result = dict(result="INCOMPLETE", hardware_tested=False, sources={})
        self.commands = []
        self.wait_idle_seconds = 0

    def run(self, command, label, env=None, cwd=None):
        # Stop between subprocesses while the shared main checkout builds.
        # The caller may wait briefly here instead of repeating passed suites;
        # never start a child while another heavy process is observed.
        if not 0 <= self.wait_idle_seconds <= 60:
            raise ValueError("Idle wait must stay within 0..60 seconds")
        deadline = time.monotonic() + self.wait_idle_seconds
        deferred = False
        while True:
            try:
                idle()
                break
            except RuntimeError:
                if time.monotonic() >= deadline:
                    raise
                if not deferred:
                    print("DEFERRED " + label + "; waiting for the shared build to finish", flush=True)
                    deferred = True
                time.sleep(min(2, max(0, deadline - time.monotonic())))
        if deferred:
            print("RESUMED " + label + "; no other heavy job observed", flush=True)
        proc = subprocess.run(command, cwd=cwd or PLATFORM, env=env, text=True, capture_output=True)
        (self.out / (label + ".log")).write_text(proc.stdout + proc.stderr)
        if proc.returncode:
            raise RuntimeError(label + " failed:\n" + (proc.stdout + proc.stderr)[-6000:])
        print(proc.stdout.strip() if label.endswith("tests") else "PASS " + label, flush=True)
        return proc.stdout

    def model(self, test_file, sources, libraries=()):
        self.run(["clang", "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror", "-fno-pie", "-no-pie",
                  "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + str(PLATFORM),
                  str(REPO / "port-tests" / test_file), *[str(PLATFORM / s) for s in sources],
                  "-o", str(self.out / "models"), *["-l" + name for name in libraries]], "compile-models")
        self.run([str(self.out / "models")], "model-tests")
        self.result["models"] = test_file
        self.result["models_sha256"] = hashlib.sha256((REPO / "port-tests" / test_file).read_bytes()).hexdigest()

    def cross_compile(self, sources):
        commands = None
        for file in sorted((REPO / "out/automation").glob("control-*/compile-commands.json"), reverse=True):
            data = json.loads(file.read_text())
            names = {item["object"] for item in data}
            if "iokit/i9100_power.o" in names and any(name in names for name in (
                    "iokit/IOS7I9100SDMMC.cpo", "iokit/IOS7I9100Power.cpo", "iokit/IOS7I9100Mali.cpo")):
                commands = {item["object"]: item for item in data}
                self.result["compile_template"] = str(file.relative_to(ROOT))
                self.result["compile_template_sha256"] = hashlib.sha256(file.read_bytes()).hexdigest()
                break
        if commands is None:
            raise RuntimeError("No retained ARM/XNU compiler templates")
        env = dict(os.environ)
        env["PATH"] = str(ROOT / "assets/i9100-toolchain/bin") + ":" + env["PATH"]
        for name in sources:
            cpp = name.endswith(".cpp")
            obj = next((name for name in ("iokit/IOS7I9100SDMMC.cpo", "iokit/IOS7I9100Power.cpo",
                        "iokit/IOS7I9100Mali.cpo") if name in commands)) if cpp else "iokit/i9100_power.o"
            command = list(commands[obj]["arguments"])
            inputs = [i for i, value in enumerate(command) if value.endswith(".cpp" if cpp else ".c")]
            if len(inputs) != 1 or "-O1" not in command:
                raise RuntimeError("Unexpected compiler template")
            destination = self.out / (name + (".cpo" if cpp else ".o"))
            command[inputs[0]] = str(PLATFORM / name)
            command[command.index("-o") + 1] = str(destination)
            for i, flag in enumerate(command[:-1]):
                if flag in ("-MF", "-MT"):
                    command[i + 1] = str(destination.with_suffix(".d") if flag == "-MF" else destination)
            command.extend(["-MF", str(destination.with_suffix(".d"))])
            self.run(command, "cross-" + name, env)
            self.commands.append(dict(source=name, object=str(destination.relative_to(ROOT)), arguments=command))
        (self.out / "compile-commands.json").write_text(json.dumps(self.commands, indent=2) + "\n")
        self.result["cross_compiled"] = True

    def model_cpp(self, test_file, c_sources, cpp_sources, libraries=()):
        """Run native bridge logic with a host-only IOKit facade, not an ARM SDK."""
        sdk = REPO / "port-tests/offline-native-sdk"
        common = ["-O1", "-Wall", "-Wextra", "-Werror", "-fno-pie",
                  "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                  "-I" + str(sdk), "-I" + str(PLATFORM)]
        objects = []
        for source in c_sources + cpp_sources:
            cpp = source.endswith(".cpp")
            destination = self.out / ("host-model-" + source + ".o")
            self.run(["clang++" if cpp else "clang", "-std=c++11" if cpp else "-std=c99",
                      *common, "-c", str(PLATFORM / source), "-o", str(destination)], "host-" + source)
            objects.append(str(destination))
        self.run(["clang++", "-std=c++11", *common, "-no-pie",
                  str(REPO / "port-tests" / test_file), *objects, "-o", str(self.out / "models"),
                  *["-l" + name for name in libraries]], "compile-native-models")
        self.run([str(self.out / "models")], "native-model-tests")
        self.result.update(native_model=test_file, native_iokit_facade=True,
                           native_model_sha256=hashlib.sha256((REPO / "port-tests" / test_file).read_bytes()).hexdigest(),
                           facade_headers={str(path.relative_to(sdk)): hashlib.sha256(path.read_bytes()).hexdigest()
                                           for path in sorted(sdk.rglob("*.h"))})

    def finish(self, sources, scope):
        for name in sources:
            self.result["sources"][name] = hashlib.sha256((PLATFORM / name).read_bytes()).hexdigest()
            header = PLATFORM / (Path(name).stem + ".h")
            if header.exists():
                self.result["sources"][header.name] = hashlib.sha256(header.read_bytes()).hexdigest()
        self.result.update(result="PASS", scope=scope, complete_kernel_link=False)
        (self.out / "manifest.json").write_text(json.dumps(self.result, indent=2) + "\n")
        print("EVIDENCE", self.out, flush=True)
