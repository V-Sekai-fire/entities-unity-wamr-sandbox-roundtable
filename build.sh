#!/usr/bin/env bash
# One command, no arguments: builds everything the native harness needs (macOS arm64).
#   brew install llvm llvm@22 lld ninja cmake zig     (Xcode command line tools for clang)
# Everything it makes is ignored by git: guests, wamrc, plugins. Nothing binary is committed.
set -euo pipefail
cd "$(dirname "$0")"
for t in cmake ninja brew clang; do command -v $t >/dev/null || { echo "missing $t (see the brew line above)" >&2; exit 1; }; done
for p in llvm llvm@22 lld; do brew --prefix $p >/dev/null 2>&1 || { echo "missing brew package $p" >&2; exit 1; }; done
[ -f deps/wamr/CMakeLists.txt ] || { echo "deps/ is empty: clone with the subtrees (no submodules), not a partial checkout" >&2; exit 1; }

# 1. wamrc, the WAMR AOT compiler (LLVM 23 is too new for WAMR 2.4.3; llvm@22 builds)
if [ ! -x bench/wamrc-build/wamrc ]; then
  cmake -S deps/wamr/wamr-compiler -B bench/wamrc-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR="$(brew --prefix llvm@22)/lib/cmake/llvm" -DWAMR_BUILD_PLATFORM=darwin
  cmake --build bench/wamrc-build
fi

# 2. the guest, for wasm32, AOT and rv64gc
WAMRC="$PWD/bench/wamrc-build/wamrc" bench/guest/build.sh

# 3. the standalone harness
cmake -S bench/host -B bench/build -G Ninja
cmake --build bench/build

echo "built. Next: ./bench.sh for the numbers (libriscv runs through Godot, see bench/usd/godot-check)"
