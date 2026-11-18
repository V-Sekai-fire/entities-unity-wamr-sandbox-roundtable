#!/usr/bin/env bash
# Case 4g, equal comparison: one workload (solves x 20 iterations), one guest source and flags, every row on the same hosts.
# usage: run.sh [solves=1000] [rounds=2]
#   pointer width: wasm32 vs wasm64 vs rv64 (all LP64 or ILP32 as the target says)
#   bounds checks: WAMR guard pages / software checks; libriscv arena mask / per-access check / none
set -euo pipefail
cd "$(dirname "$0")"
S=${1:-1000}; R=${2:-2}; O=$PWD/out
mkdir -p $O/bt && cd $O/bt        # libriscv caches its translation here, as rvbintr-*
H=$O/host64/rdbench               # one WAMR build (memory64, classic interpreter) for every wamr row
row() { echo "== $1"; shift; "$@"; }
export CC=clang CFLAGS="-O3 -fno-inline"
row "wasm32 AOT, guard pages"          $H wamr $O/rdbench.aot $S $R
row "wasm32 AOT, software bounds"      $H wamr $O/rdbench.sw.aot $S $R
row "wasm64 AOT (software bounds)"     $H wamr $O/rdbench64.aot $S $R
row "rv64 translated, arena mask"      env RT_BOUNDS=mask  $H riscv-bt $O/rdbench.rv64.elf $S $R
row "rv64 translated, checked"         env RT_BOUNDS=check $H riscv-bt $O/rdbench.rv64.elf $S $R
row "rv64 translated, unchecked"       env RT_BOUNDS=none  $H riscv-bt $O/rdbench.rv64.elf $S $R
row "wasm32 interpreter (classic)"     $H wamr $O/rdbench.wasm $S 1
row "wasm64 interpreter (classic)"     $H wamr $O/rdbench64.wasm $S 1
row "rv64 interpreter"                 $H riscv-interp $O/rdbench.rv64.elf $S 1
