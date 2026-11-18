#!/usr/bin/env bash
# One command, no arguments: builds everything the Unity project and the native harness need (macOS arm64).
#   brew install llvm llvm@22 lld ninja cmake
set -euo pipefail
cd "$(dirname "$0")"

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

# 4. the Unity plugin and the guests it loads
mkdir -p Assets/Plugins/macOS Assets/Plugins/x86_64 Assets/StreamingAssets
#    macOS arm64
cmake -S native -B native/build -G Ninja
cmake --build native/build
cp native/build/libub_native.dylib Assets/Plugins/macOS/
#    Windows x86_64, cross-compiled with zig's clang (mingw-w64 ABI); no binary translation there
cmake -S native -B native/build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PWD/native/cmake/mingw-w64.cmake"
cmake --build native/build-win
cp native/build-win/ub_native.dll Assets/Plugins/x86_64/
cp bench/guest/guest.wasm bench/guest/guest.aot bench/guest/guest.win.aot bench/guest/guest.rv64.elf Assets/StreamingAssets/
echo "built: open this folder in Unity 2022.3.22f1, then Benchmark > Run all"
