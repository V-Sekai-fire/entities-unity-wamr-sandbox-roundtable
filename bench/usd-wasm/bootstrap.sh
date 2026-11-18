#!/usr/bin/env bash
# Builds usd.wasm, usd.aot and the usdbench driver from nothing: wasi-sdk, oneTBB and OpenUSD are downloaded
# into .cache/usd-wasm (override with WASMUSD=...). Idempotent: finished steps are skipped. ~20 min on an M-series Mac.
# Needs: git, cmake, ninja, python3, curl, and bench/wamrc-build/wamrc (./build.sh at the repo root makes it).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
export WASMUSD=${WASMUSD:-$ROOT/.cache/usd-wasm}
R=$WASMUSD; SHIM=$HERE/shim
mkdir -p "$R/src"

# --- 1. wasi-sdk 34 -------------------------------------------------------------------------------------------
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) SDK=arm64-macos;; Darwin-x86_64) SDK=x86_64-macos;;
  Linux-x86_64) SDK=x86_64-linux;; Linux-aarch64) SDK=arm64-linux;;
  *) echo "no wasi-sdk build for $(uname -sm)" >&2; exit 1;;
esac
W=$R/wasi-sdk-34.0-$SDK
if [ ! -x "$W/bin/clang" ]; then
  curl -fL "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-34.0-$SDK.tar.gz" | tar xz -C "$R"
fi
export W

# --- 2. sources, pinned ---------------------------------------------------------------------------------------
fetch() { # dir url rev (tag or commit): shallow fetch, then check out what was fetched
  if [ ! -d "$1/.git" ]; then git init -q "$1"; git -C "$1" remote add origin "$2"; fi
  git -C "$1" fetch -q --depth 1 origin "$3"
  git -C "$1" checkout -q --force FETCH_HEAD
}
patch_once() { # dir patch
  if git -C "$1" apply --reverse --check "$2" 2>/dev/null; then return; fi
  git -C "$1" apply "$2"
}
fetch "$R/src/oneTBB" https://github.com/uxlfoundation/oneTBB.git v2021.12.0
fetch "$R/src/usd" https://github.com/V-Sekai-fire/repository-openusd.git 7b709ed624bed1a25dd20e4a1875595bc77ac7c3
fetch "$R/src/guest-common" https://github.com/V-Sekai-fire/contract-guest-common.git 1ea61e04bd8e91dfabbf9f36c58de736e5540077
patch_once "$R/src/oneTBB" "$HERE/patches/onetbb-wasi.patch"
patch_once "$R/src/usd" "$HERE/patches/openusd-wasm32-wasi.patch"

# --- 3. oneTBB (one thread, no exceptions) --------------------------------------------------------------------
if [ ! -f "$R/inst/lib/libtbb.a" ]; then
  CXXFLAGS="-fignore-exceptions -D__unix__=1 -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_SIGNAL -include cstdlib -include algorithm" \
  CFLAGS="-D__unix__=1 -D_WASI_EMULATED_MMAN" \
  cmake -S "$R/src/oneTBB" -B "$R/tbb" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$W/share/cmake/wasi-sdk-p1.cmake" -DWASI_SDK_PREFIX="$W" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$R/inst" -DBUILD_SHARED_LIBS=OFF -DTBB_TEST=OFF -DTBB_EXAMPLES=OFF \
    -DTBB_STRICT=OFF -DTBBMALLOC_BUILD=OFF -DTBB_ENABLE_IPO=OFF
  ninja -C "$R/tbb" install
fi

# --- 4. OpenUSD, static, as Emscripten-shaped wasm32 ----------------------------------------------------------
if [ ! -f "$R/usd/pxr/usd/usdSkel/libusd_usdSkel.a" ]; then
  F="-include $SHIM/compat.h -fignore-exceptions -D__unix__=1 -D__EMSCRIPTEN__=1 -I$SHIM -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_GETPID -DTBB_USE_EXCEPTIONS=0 -ffile-prefix-map=$R/src/usd/=openusd/"
  CXXFLAGS="$F -include cstdlib -include algorithm" CFLAGS="$F" \
  cmake -S "$R/src/usd" -B "$R/usd" -G Ninja -DEMSCRIPTEN=ON -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DCMAKE_TOOLCHAIN_FILE="$W/share/cmake/wasi-sdk-p1.cmake" -DWASI_SDK_PREFIX="$W" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF -DPXR_BUILD_MONOLITHIC=OFF -DPXR_ENABLE_PYTHON_SUPPORT=OFF -DPXR_BUILD_IMAGING=OFF \
    -DPXR_BUILD_USD_IMAGING=OFF -DPXR_BUILD_USDVIEW=OFF -DPXR_BUILD_TESTS=OFF -DPXR_BUILD_EXAMPLES=OFF \
    -DPXR_BUILD_TUTORIALS=OFF -DPXR_BUILD_USD_TOOLS=OFF -DPXR_BUILD_HTML_DOCUMENTATION=OFF \
    -DPXR_ENABLE_MATERIALX_SUPPORT=OFF -DPXR_ENABLE_OPENVDB_SUPPORT=OFF -DPXR_ENABLE_PTEX_SUPPORT=OFF \
    -DPXR_BUILD_ALEMBIC_PLUGIN=OFF -DPXR_BUILD_DRACO_PLUGIN=OFF -DPXR_ENABLE_OSL_SUPPORT=OFF \
    -DPXR_PREFER_SAFETY_OVER_SPEED=ON -DTBB_ROOT="$R/inst" -DTBB_DIR="$R/inst/lib/cmake/TBB"
  ninja -C "$R/usd"
fi

# --- 5. the guest, its AOT image, and the driver --------------------------------------------------------------
"$HERE/build.sh"
WAMRC=${WAMRC:-$ROOT/bench/wamrc-build/wamrc}
[ -x "$WAMRC" ] || { echo "no wamrc at $WAMRC: run ./build.sh at the repo root first" >&2; exit 1; }
"$WAMRC" --opt-level=3 --enable-multi-thread -o "$HERE/out/usd.aot" "$HERE/out/usd.wasm"
cmake -S "$HERE" -B "$HERE/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$HERE/build"
ls -la "$HERE/out/usd.wasm" "$HERE/out/usd.aot" "$HERE/build/usdbench"
