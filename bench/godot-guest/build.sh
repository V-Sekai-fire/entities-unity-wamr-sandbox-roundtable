#!/usr/bin/env bash
# Builds bench_guest.elf with zig (riscv64-linux-musl), double precision. Needs: zig, cmake, ninja.
set -euo pipefail
cd "$(dirname "$0")"
export CC="zig;cc;-target riscv64-linux-musl" CXX="zig;c++;-target riscv64-linux-musl"
cmake -S . -B build -G Ninja -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=riscv64 -DCMAKE_OSX_ARCHITECTURES= -DCMAKE_OSX_SYSROOT= -DCMAKE_AR="$(brew --prefix llvm)/bin/llvm-ar" -DCMAKE_RANLIB="$(brew --prefix llvm)/bin/llvm-ranlib" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"
cmake --build build
ls -l build/bench_guest.elf
