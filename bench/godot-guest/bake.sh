#!/usr/bin/env bash
# Bakes the binary translation Godot's Sandbox loads for bench_guest.elf: project/bintr/bintr-<hash>.dylib.
# Uses godot-sandbox's own libriscv pin (ec5b426b, ext/libriscv) and the MachineOptions Sandbox builds its machine with.
# BAKE_NOLIMIT=1 bakes for execution_timeout = 0 (no instruction counting); default bakes for the editor's default.
# Check: the printed hash must equal Sandbox.get_translation_hash() in the editor (run.sh prints it).
set -euo pipefail
cd "$(dirname "$0")"
PIN=ec5b426b0626f3aadeadc38d894ac156a0998008
[ -d ../../.cache/libriscv-pin ] || { mkdir -p ../../.cache && git clone -q https://github.com/fwsGonzo/libriscv ../../.cache/libriscv-pin; }
git -C ../../.cache/libriscv-pin checkout -q $PIN
cmake -S bake -B bake/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIBRISCV_DIR="$PWD/../../.cache/libriscv-pin" >/dev/null
cmake --build bake/build
rm -rf project/bintr && mkdir -p project/bintr
CC=clang CFLAGS="${CFLAGS:--O3 -fno-inline}" bake/build/bake build/bench_guest.elf project/bintr 256
