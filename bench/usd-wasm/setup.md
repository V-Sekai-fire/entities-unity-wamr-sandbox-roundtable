# Building usd.wasm

`usd.wasm` is the OpenUSD guest core (`deps/interactor-usd-guest/guest/usd/usd_core.cpp` and `mem_resolver.cpp`)
built for wasm32-wasip1 with a plain C ABI (`main_wasm.cpp`) instead of godot-sandbox's Variant API.
It reads the same `.usdz` and reports the same counts as `usd.elf`.

1. wasi-sdk 34 (`wasi-sdk-34.0-arm64-macos.tar.gz`) unpacked in `~/wasmusd`.
2. `git clone --depth 1 -b v2021.12.0 https://github.com/uxlfoundation/oneTBB src/oneTBB && git -C src/oneTBB apply bench/usd-wasm/patches/onetbb-wasi.patch`
3. `git clone https://github.com/V-Sekai-fire/repository-openusd src/usd && git -C src/usd checkout 7b709ed624bed1a25dd20e4a1875595bc77ac7c3 && git -C src/usd apply bench/usd-wasm/patches/openusd-wasm32-wasi.patch`
4. `git clone https://github.com/V-Sekai-fire/contract-guest-common src/guest-common` at `1ea61e04bd8e91dfabbf9f36c58de736e5540077` (blake3.h).
5. Copy `shim/` and `cfg-usd.sh` to `~/wasmusd`. Build oneTBB with `-fignore-exceptions -D__unix__=1 -D_WASI_EMULATED_MMAN -include cstdlib -include algorithm`
   (single thread, no tbbmalloc), then `./cfg-usd.sh && ninja -C usd`.
6. `bench/usd-wasm/build.sh` links `out/usd.wasm`; `wamrc --opt-level=3 --enable-multi-thread` makes `out/usd.aot`.
7. `cmake -S bench/usd-wasm -B bench/usd-wasm/build && cmake --build bench/usd-wasm/build`, then
   `usdbench out/usd.aot Mire.usdz 5`.

What the port changes, and why:

- **C++ exceptions are compiled out** (`-fignore-exceptions`): WAMR has no wasm exception handling. A `throw` calls a stub that prints the type and aborts. A malformed `.usda` therefore aborts instead of reporting a parse error; `.usdc` inside a `.usdz` is the path the benchmark uses.
- **No threads**: oneTBB runs on the calling thread. `usd.elf` is single-threaded too.
- **No virtual memory**: `ArchReserveVirtualMemory` is a zeroed allocation; Sdf's path pools use 14 region bits instead of 8 so a region is 5 MB, not 335 MB.
- **Signals, fork, dlopen, statfs** are stubbed in `shim/` (`compat.h`, `compat.c`): crash handlers and stack traces do nothing.
- OpenUSD is told it is Emscripten (`-D__EMSCRIPTEN__=1`, `-DEMSCRIPTEN=ON`) so its existing wasm32 code paths apply.
