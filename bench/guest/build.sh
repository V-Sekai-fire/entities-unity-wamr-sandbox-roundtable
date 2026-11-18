#!/usr/bin/env bash
# Build the guest for both runtimes. Needs Homebrew llvm (wasm32, wasm-ld) and riscv64-elf-gcc.
set -e
cd "$(dirname "$0")"
LLVM=$(brew --prefix llvm)/bin
$LLVM/clang --target=wasm32 -O2 -fno-math-errno -nostdlib -Wl,--no-entry -Wl,--allow-undefined -fuse-ld=$(command -v wasm-ld) \
  -Wl,--export=empty -Wl,--export=update -Wl,--export=transforms -Wl,--export=transforms_soa -Wl,--export=lbfgs \
  -Wl,--initial-memory=2097152 -o guest.wasm guest.c
riscv64-elf-gcc -O2 -fno-math-errno -mno-relax -march=rv64gc -mabi=lp64d -ffreestanding -nostdlib -static -Wl,-e,_start -Wl,-z,separate-code -Wl,-z,max-page-size=4096 -Wl,--no-warn-rwx-segments \
  -o guest.rv64.elf guest.c
ls -l guest.wasm guest.rv64.elf
# AOT for the host CPU (wamrc is built from deps/wamr/wamr-compiler against Homebrew llvm@22)
WAMRC=${WAMRC:-../wamrc-build/wamrc}
$WAMRC --opt-level=3 -o guest.aot guest.wasm
ls -l guest.aot
