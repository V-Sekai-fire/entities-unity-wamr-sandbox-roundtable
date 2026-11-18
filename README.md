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

### Results inside Unity 2022.3.22f1 (macOS arm64 Editor)

Milliseconds; raw output in `bench/results-unity.json`. Case 2 moves 10,000 real `Transform`s through managed callbacks (reads `localPosition` and `localRotation`, writes `localPosition`).

| Case | wamr-interp | wamr-aot | libriscv-interp | libriscv-bt |
|---|---|---|---|---|
| 1. 1,000,000 empty calls, loop in C# (each call crosses P/Invoke) | 277.7 | 275.8 | 20.3 | 24.6 |
| 1. Same, loop in native code | 260.8 | 249.6 | 6.6 | 10.9 |
| 2. 10,000 transforms, AOS (median of 200) | 3.055 | 2.964 | 2.867 | 2.754 |
| 2. 10,000 transforms, SOA (median of 200) | 2.612 | 2.496 | 2.621 | 2.513 |
| 3. Load, instantiate, first update (median of 200) | 0.229 | 0.153 | 4.746 | 6.149 |
| 3. Same, first run (libriscv-bt reused the translation cached by an earlier run) | 1.165 | 0.578 | 5.763 | 8.032 |
| 4. L-BFGS, 1,000,000 iterations | 2871.9 | 249.3 | 5754.7 | 1736.3 |

### Six runs in the Editor

Median (min–max) of five runs, in milliseconds. Run 2 is left out of the table because the whole run was 4 to 10 times slower (for example WAMR AOT L-BFGS 2525 ms against about 250 ms, WAMR interpreter native empty-call loop 1004 ms against about 265 ms), most likely the Editor throttled while another job used the machine. Its file is kept in `bench/unity-runs/run2.json`. Each run is a separate **Benchmark > Run all** in an open Editor; the libriscv-bt first-run row reuses the on-disk translation, so it is not a cold compile.

|Case|wamr-interp|wamr-aot|libriscv-interp|libriscv-bt|
|---|---|---|---|---|
|1_empty_call_from_csharp|278.347 (276.565–294.772)|275.822 (270.194–284.149)|21.633 (20.336–22.397)|25.115 (24.467–25.774)|
|1_empty_call_native_loop|264.251 (260.840–277.599)|254.416 (249.645–263.055)|6.718 (6.572–7.137)|10.626 (9.953–10.870)|
|2_transforms_10k_aos|3.108 (3.049–3.145)|2.964 (2.950–3.121)|2.870 (2.867–3.049)|2.754 (2.748–2.873)|
|2_transforms_10k_soa|2.654 (2.612–2.717)|2.575 (2.496–2.687)|2.689 (2.621–2.803)|2.554 (2.513–2.651)|
|3_load_first_update|0.207 (0.194–0.239)|0.147 (0.118–0.170)|4.746 (4.164–4.881)|5.931 (5.629–6.360)|
|3_load_first_update_first_ever|0.303 (0.247–1.165)|0.213 (0.157–0.578)|4.885 (4.684–5.763)|6.464 (6.140–8.032)|
|4_lbfgs|2883.072 (2871.860–2928.032)|250.688 (246.701–256.383)|5774.228 (5754.738–5914.860)|1748.985 (1736.346–1764.382)|

The five clean runs agree within about 1 to 3 percent on the totals and about 10 percent on the per-load medians (case 3). The ordering in every row is the same in all six runs.

Case 2 checks: every runtime leaves the transforms summing to 60,000. Case 4 residuals agree to 7 digits.

- In Unity, case 2 is the same for all four runtimes (2.5 to 3.1 ms). The Unity `Transform` access dominates, so the runtime choice does not matter for host-API-heavy work. In the native harness the runtimes differed by 4x.
- Case 1 shows the P/Invoke boundary adds about 14 ms per million calls for libriscv (20.3 against 6.6). It is invisible next to WAMR's 250 ns per call.
- Case 3 here loads the 5 KB test guest, not an avatar module.

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
