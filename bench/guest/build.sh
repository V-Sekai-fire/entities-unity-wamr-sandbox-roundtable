#!/usr/bin/env bash
# Build the guest for both runtimes. Needs Homebrew llvm and lld (wasm32, rv64gc, wasm-ld, ld.lld).
set -euo pipefail
cd "$(dirname "$0")"
LLVM=$(brew --prefix llvm)/bin
LLD=$(brew --prefix lld)/bin

$LLVM/clang --target=wasm32 -O2 -fno-math-errno -nostdlib -Wl,--no-entry -Wl,--allow-undefined -fuse-ld=$(command -v wasm-ld) \
  -Wl,--export=empty -Wl,--export=update -Wl,--export=transforms -Wl,--export=transforms_soa -Wl,--export=lbfgs \
  -Wl,--initial-memory=2097152 -o guest.wasm guest.c

# rv64 with the same compiler as the wasm, so case 4 compares runtimes and not GCC against clang
$LLVM/clang --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64d -O2 -fno-math-errno -ffreestanding -nostdlib \
  -mno-relax -fuse-ld=$LLD/ld.lld -static -Wl,-e,_start -Wl,-z,separate-code -Wl,-z,max-page-size=4096 \
  -o guest.rv64.elf guest.c
ls -l guest.wasm guest.rv64.elf

# AOT for the host CPU and for Windows x86_64 (wamrc is built from deps/wamr/wamr-compiler against Homebrew llvm@22)
WAMRC=${WAMRC:-../wamrc-build/wamrc}
$WAMRC --opt-level=3 -o guest.aot guest.wasm
$WAMRC --target=x86_64 --target-abi=gnu --cpu=x86-64-v2 --opt-level=3 -o guest.win.aot guest.wasm
ls -l guest.aot guest.win.aot
