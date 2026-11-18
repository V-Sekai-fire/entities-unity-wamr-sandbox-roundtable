#!/usr/bin/env bash
# Cases 1 and 4 on one guest source (guest/guest_min.c) built for wasm32, wasm64 and rv64, every row on the same runtime build.
# usage: eq.sh [rounds=3] [solves=50000]. Needs guest/build_eq.sh and: cmake -S host -B build64 -G Ninja -DWAMR_MEMORY64=ON
set -euo pipefail
cd "$(dirname "$0")"
R=${1:-3}; S=${2:-50000}; E=build64/eq; G=guest/eq
mkdir -p build64/bt && cd build64/bt
E=../eq; G=../../guest/eq
export CC=clang CFLAGS="-O3 -fno-inline"
row() { echo "== $1"; shift; "$@"; }
row "wasm32 AOT, guard pages"      $E wamr $G/min32.aot $R $S
row "wasm32 AOT, software bounds"  $E wamr $G/min32.sw.aot $R $S
row "wasm64 AOT (software bounds)" $E wamr $G/min64.aot $R $S
row "rv64 translated, arena mask"  env RT_BOUNDS=mask  $E riscv-bt $G/min.rv64.elf $R $S
row "rv64 translated, checked"     env RT_BOUNDS=check $E riscv-bt $G/min.rv64.elf $R $S
row "rv64 translated, unchecked"   env RT_BOUNDS=none  $E riscv-bt $G/min.rv64.elf $R $S
row "wasm32 interpreter (classic)" $E wamr $G/min32.wasm 1 $S
row "wasm64 interpreter (classic)" $E wamr $G/min64.wasm 1 $S
row "rv64 interpreter"             $E riscv-interp $G/min.rv64.elf 1 $S
