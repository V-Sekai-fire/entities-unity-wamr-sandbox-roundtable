#!/usr/bin/env bash
# Build usd.wasm. Needs ~/wasmusd from bench/usd-wasm/setup.md (wasi-sdk 34, oneTBB and OpenUSD built for wasm32-wasip1).
set -euo pipefail
cd "$(dirname "$0")"
R=${WASMUSD:-$HOME/wasmusd}; W=$R/wasi-sdk-34.0-arm64-macos
G=$(cd ../../deps/interactor-usd-guest/guest/usd && pwd)
T=$(cd ../../deps/interactor-usd-guest/tools && pwd)
mkdir -p out
USD_SRC=$R/src/usd python3 $T/gen_resources.py $R/usd out/usd_resources.inc
F="-include $R/shim/compat.h -fignore-exceptions -D__unix__=1 -D__EMSCRIPTEN__=1 -I$R/shim -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_GETPID -DPXR_STATIC=1 -DTBB_USE_EXCEPTIONS=0 -DMFB_PACKAGE_NAME=usdProbe -DMFB_ALT_PACKAGE_NAME=usdProbe -std=gnu++17 -O2 -fno-math-errno -ffp-contract=off -include cstdlib -include algorithm -I. -Iout -I$G -I$R/src/guest-common/guest -I$R/usd/include -I$R/inst/include -I$R/src/usd"
CXX="$W/bin/clang++ --target=wasm32-wasip1"
for f in $G/usd_core.cpp $G/mem_resolver.cpp main_wasm.cpp; do $CXX $F -c $f -o out/$(basename $f .cpp).o & done
$W/bin/clang --target=wasm32-wasip1 -O2 -D_WASI_EMULATED_SIGNAL -I$R/shim -c $R/shim/compat.c -o out/compat.o
wait
LIBS=""
for l in usdSkel usdShade usdGeom sdr usd pcp kind sdf ar ts plug trace work vt js gf tf arch; do LIBS="$LIBS $(find $R/usd/pxr -name libusd_$l.a)"; done
$CXX -mexec-model=reactor -Wl,--no-entry -Wl,--error-limit=0 -Wl,-z,stack-size=8388608 -Wl,--initial-memory=268435456 -Wl,--max-memory=4294901760 \
  -lwasi-emulated-mman -lwasi-emulated-process-clocks -lwasi-emulated-signal -lwasi-emulated-getpid \
  out/usd_core.o out/mem_resolver.o out/main_wasm.o out/compat.o -Wl,--whole-archive $LIBS -Wl,--no-whole-archive $R/inst/lib/libtbb.a -o out/usd.wasm
ls -la out/usd.wasm
