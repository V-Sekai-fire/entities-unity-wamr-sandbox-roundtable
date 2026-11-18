# entities-unity-wamr-sandbox-roundtable

Benchmark comparing WAMR and godot-sandbox as script runtimes inside Unity Engine.

## Cases

1. **Empty call.** Call an exported empty function 1,000,000 times.
2. **Host API transforms.** 10,000 transform reads and writes through the host API.
3. **Cold load.** Load a downloaded module, instantiate it, and run its first update. The module is the OpenUSD guest.
4. **Compute.** 1,000,000 iterations of a complex math problem: L-BFGS (limited-memory BFGS) solving a saddle problem.

## Unity project

The repository root is a Unity 2022.3.22f1 project. `build.sh` (one command, no arguments, macOS arm64 host) builds everything it loads:

- `Assets/Plugins/macOS/libub_native.dylib` (arm64) and `Assets/Plugins/x86_64/ub_native.dll` (Windows x86_64, cross-compiled with zig's clang). Both come from `native/`, which wraps WAMR and libriscv behind a small C ABI (`native/ub_native.cpp`). The Windows build has no libriscv binary translation, because that compiles C with the system compiler at run time.
- `Assets/StreamingAssets/guest.*`: the guest as wasm, AOT (macOS arm64 and Windows x86_64) and rv64.
- `Assets/Benchmark/Editor`: the runner (menu **Benchmark > Run all**) and 10,000 real Unity `Transform`s as the host API. Writes `results-unity.json`.

The Windows DLL is built and its exports and imports are checked (only system DLLs), but it has not been run on Windows.

## Avatars

`tools/fbx2usd.py` imports an FBX and exports OpenUSD through Blender. The avatars are `chibifire-stages/character-mille-mire-feuille` (ships `usd/Mire.usda`) and `chibifire-stages/character-marocchino` (FBX, converted with the script). Both are Apache-2.0. They are not loaded by the benchmark yet.

## Run

    brew install llvm llvm@22 riscv64-elf-gcc ninja cmake
    # wamrc, the WAMR AOT compiler. LLVM 23 is too new for WAMR 2.4.3; llvm@22 builds.
    cmake -S deps/wamr/wamr-compiler -B bench/wamrc-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_DIR=$(brew --prefix llvm@22)/lib/cmake/llvm -DWAMR_BUILD_PLATFORM=darwin
    cmake --build bench/wamrc-build
    bench/guest/build.sh            # guest.wasm, guest.aot, guest.rv64.elf
    cmake -S bench/host -B bench/build -G Ninja && cmake --build bench/build
    cd bench && CC=clang CFLAGS=-O3 build/bench guest/guest.wasm guest/guest.aot guest/guest.rv64.elf 1

The last argument divides the iteration counts (1 = full size). Run from `bench/`: libriscv writes its translation cache (`rvbintr-*`) to the working directory.

## What each runtime is set up to do

- **wamr-interp:** WAMR fast interpreter.
- **wamr-aot:** the same wasm compiled ahead of time by `wamrc --opt-level=3` for the host CPU. Loaded with the same `wasm_runtime_load` call.
- **libriscv-interp:** libriscv's C++ API with a 64-bit-only build, flat read-write arena, threaded dispatch, prepared calls (`PreparedCall`), direct guest-memory pointers in the host API (`memarray`), and no instruction counting.
- **libriscv-bt:** the same plus binary translation. libtcc is off, so the C99 translation is compiled by the system compiler (`CC=clang CFLAGS=-O3`) into a shared object, cached on disk and loaded at construction.

Neither side uses gas. Case 4 gives both the same budget: none.

## Results

Native macOS arm64 harness, not yet inside Unity. One guest source (`bench/guest/guest.c`) is built for wasm32 and rv64gc. Times are milliseconds. Raw output is in `bench/results.json`.

| Case | wamr-interp | wamr-aot | libriscv-interp | libriscv-bt |
|---|---|---|---|---|
| 1. Empty export, 1,000,000 host calls (total) | 264.4 | 253.0 | 6.8 | 11.6 |
| 2. 10,000 transforms, AOS, one host call each (median of 200) | 0.476 | 0.280 | 0.219 | 0.120 |
| 2. 10,000 transforms, SOA, 24 column calls (median of 200) | 0.130 | 0.022 | 0.147 | 0.042 |
| 3. Load from disk, instantiate, first update, cache warm (median of 200) | 0.206 | 0.072 | 0.674 | 1.226 |
| 3. Same, first ever (translation compiled and cached) | 0.463 | 0.154 | 5.597 | 928.9 |
| 4. L-BFGS saddle, 1,000,000 iterations (total) | 2984.8 | 249.1 | 7321.6 | 1877.3 |

Checks: every runtime leaves the host transforms summing to 60,000 in both layouts. The L-BFGS residual sums agree to 7 digits (1.40992567e-14 for WAMR, 1.40992534e-14 for libriscv). They are not identical because the rv64 build uses fused multiply-add.

Reading the table:

- libriscv wins the host-boundary cases (1 and 2 AOS). Each call is a register setup and a jump. A WAMR call goes through `wasm_runtime_call_wasm`, which costs about 250 ns even for AOT code.
- WAMR AOT wins compute (case 4) by 7.5x over libriscv's binary translation, and wins the SOA case, where the guest loop dominates.
- libriscv binary translation costs 929 ms the first time and 1.2 ms to load afterwards. A shipped build needs the translation pre-generated and embedded; libriscv supports embedding the generated source.
- WAMR AOT loads fastest (0.072 ms).

What this does not show:

- Unity is not in the loop. These numbers exclude the P/Invoke boundary, which will add cost to every host call.
- Case 3 loads a 5 KB test guest, not the OpenUSD module.
- libriscv is built here as 64-bit only with its default arena. godot-sandbox guests use libriscv's own build options; sandbox-host adds gas and a Variant wire that this harness does not.
- No gas on either side, which sandbox-host needs.
- One machine, one run of the totals. No variance for cases 1 and 4.
- The two compilers differ: wasm comes from LLVM 23 clang, rv64 from GCC.

## Status

Cases 1 to 4 run natively with the test guest, in interpreter and AOT or binary-translation modes. Not built yet: the Unity host for either runtime and the OpenUSD guest.

## Licence

MIT. See [LICENSE](LICENSE).
