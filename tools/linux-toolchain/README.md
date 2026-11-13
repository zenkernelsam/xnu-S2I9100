# Linux toolchain retained for I9100

The active rebuild uses the existing ELF tools in the sibling project directory
`assets/i9100-toolchain/bin`. `manifest.json` there records hashes and provenance.
The complete original cctools/MIG source and build environment is retained in
`research/archive/maintenance-20261113/wsl-toolchain-before.tar.gz`.

These scripts wrap clang, llvm-ar and migcom for the inherited ARMv7 Mach-O build.
They are versioned here so the wrappers are no longer loose files under `/root`.
MIG temporary preprocessing input stays in `/tmp`; the wrapper never deletes it.
Compile and link under the project CPU envelope. The incremental build entry is
`tools/rebuild-i9100.py`; no old `/root/i9100-toolchain` dependency is required.
