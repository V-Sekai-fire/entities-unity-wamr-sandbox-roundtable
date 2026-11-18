#!/usr/bin/env bash
# One command, no arguments: builds everything the Unity project and the native harness need (macOS arm64).
#   brew install llvm llvm@22 lld ninja cmake zig     (zig only for the Windows DLL; Xcode command line tools for clang)
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

# 4. the Unity plugin and the guests it loads
mkdir -p Assets/Plugins/macOS Assets/Plugins/x86_64 Assets/StreamingAssets
#    macOS arm64
cmake -S native -B native/build -G Ninja -DUB_EMBED_SRC=
cmake --build native/build
#    Bake libriscv's binary translation of the rv64 guest ahead of time and compile it into the plugin, so the
#    shipped dylib needs no C compiler. The compile of the baked source takes about a minute.
rm -rf native/baked && CC=clang CFLAGS="-O2 -fno-inline" native/build/ub_bake bench/guest/guest.rv64.elf native/baked
BAKED=$(ls native/baked/ub-*.cpp)
cmake -S native -B native/build -G Ninja -DUB_EMBED_SRC="$PWD/$BAKED"
cmake --build native/build
cp native/build/libub_native.dylib Assets/Plugins/macOS/
#    Windows x86_64, cross-compiled with zig's clang (mingw-w64 ABI); no binary translation there yet. Skipped without zig.
if command -v zig >/dev/null; then
  cmake -S native -B native/build-win -G Ninja -DUB_EMBED_SRC= -DCMAKE_TOOLCHAIN_FILE="$PWD/native/cmake/mingw-w64.cmake"
  cmake --build native/build-win
  cp native/build-win/ub_native.dll Assets/Plugins/x86_64/
else
  echo "zig not found: skipping the Windows DLL" >&2
fi
cp bench/guest/guest.wasm bench/guest/guest.aot bench/guest/guest.win.aot bench/guest/guest.rv64.elf Assets/StreamingAssets/
echo "built. Next: ./bench.sh for the numbers without Unity, or open this folder in Unity 2022.3.22f1 and run Benchmark > Run all"
