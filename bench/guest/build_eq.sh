#!/usr/bin/env bash
# The equal-comparison guests (guest_min.c): the same source, compiler and flags for every target, in eq/.
#   min32.wasm / min64.wasm   wasm32 and wasm64 (memory64): 32- and 64-bit pointers
#   min32.aot                 wasm32, guard-page bounds checks (what WAMR does by default on 64-bit hosts)
#   min32.sw.aot              wasm32, software bounds checks
#   min64.aot                 wasm64: WAMR cannot use guard pages for a 64-bit memory, so the checks are software
#   min.rv64.elf              rv64gc, LP64 like wasm64
set -euo pipefail
cd "$(dirname "$0")"
LLVM=$(brew --prefix llvm)/bin; LLD=$(brew --prefix lld)/bin
WAMRC=${WAMRC:-../wamrc-build/wamrc}
mkdir -p eq
COMMON="-O2 -fno-math-errno -ffp-contract=off -ffreestanding -nostdlib"
for t in 32 64; do
  $LLVM/clang --target=wasm$t $COMMON -Wl,--no-entry -fuse-ld=$(command -v wasm-ld) \
    -Wl,--export=empty -Wl,--export=update -Wl,--export=transforms -Wl,--export=transforms_soa -Wl,--export=lbfgs \
    -Wl,--initial-memory=2097152 -o eq/min$t.wasm guest_min.c
done
$LLVM/clang --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64d $COMMON -mno-relax -fuse-ld=$LLD/ld.lld -static \
  -Wl,-e,_start -Wl,-z,separate-code -Wl,-z,max-page-size=4096 -o eq/min.rv64.elf guest_min.c
$WAMRC --opt-level=3 -o eq/min32.aot eq/min32.wasm
$WAMRC --opt-level=3 --bounds-checks=1 -o eq/min32.sw.aot eq/min32.wasm
$WAMRC --opt-level=3 --bounds-checks=1 -o eq/min64.aot eq/min64.wasm
ls -l eq
