# entities-unity-wamr-sandbox-roundtable

Benchmark comparing WAMR and godot-sandbox as script runtimes inside Unity Engine.

## Reproduce

Host: macOS on Apple silicon (the Windows DLL is cross-built; the Windows numbers are not measured). Nothing binary is in git; every step below builds from the sources and the pinned `deps/` subtrees.

```sh
brew install llvm llvm@22 lld ninja cmake zig   # zig only for the Windows DLL; Xcode command line tools for clang
git clone https://github.com/V-Sekai-fire/entities-unity-wamr-sandbox-roundtable && cd entities-unity-wamr-sandbox-roundtable
./build.sh                          # wamrc, the guests (wasm, AOT, rv64), the harness, the Unity plugins
./bench.sh                          # cases 1, 2, 4 without Unity; writes bench/results.json
```

Case 3 with the real OpenUSD module (about 20 minutes for the first build, needs ~10 GB disk in `.cache/`):

```sh
bench/usd/fetch.sh                  # Mire.usdz, Marocchino.usdz, usd.elf: pinned, sha256 checked
bench/usd-wasm/bootstrap.sh         # wasi-sdk, oneTBB, OpenUSD -> usd.wasm, usd.aot, usdbench
./bench.sh                          # now also runs case 3 under WAMR
```

Inside Unity (2022.3.22f1, installed through Unity Hub): open the folder and choose **Benchmark > Run all**, or headless:

```sh
/Applications/Unity/Hub/Editor/2022.3.22f1/Unity.app/Contents/MacOS/Unity -batchmode -nographics \
  -projectPath "$PWD" -executeMethod Benchmark.BenchRunner.RunBatch -quit
```

It writes `results-unity.json`.

Not reproducible from this repository alone: **case 3 on libriscv with the real `usd.elf`**. That guest speaks godot-sandbox's host ABI, which only `chibifire-stages/sandbox-host` (private) implements outside Godot. `bench/usd/results.md` records that run; it is not a like-for-like comparison with WAMR until it uses the same translation settings.

Timings vary by machine and by what else is running; the checked-in `bench/results.json` and `bench/unity-runs/` are the runs behind the tables below.

## Cases

1. **Empty call.** Call an exported empty function 1,000,000 times.
2. **Host API transforms.** 10,000 transform reads and writes through the host API.
3. **Cold load.** Load a downloaded module, instantiate it, and run its first update. The module is the OpenUSD guest.
4. **Compute.** 1,000,000 iterations of a complex math problem: L-BFGS (limited-memory BFGS) solving a saddle problem.

## Unity project

The repository root is a Unity 2022.3.22f1 project. `./build.sh` builds everything it loads:

- `Assets/Plugins/macOS/libub_native.dylib` (arm64) and `Assets/Plugins/x86_64/ub_native.dll` (Windows x86_64, cross-compiled with zig's clang). Both come from `native/`, which wraps WAMR and libriscv behind a small C ABI (`native/ub_native.cpp`). The Windows build has no libriscv binary translation, because that compiles C with the system compiler at run time.
- `Assets/StreamingAssets/guest.*`: the guest as wasm, AOT (macOS arm64 and Windows x86_64) and rv64.
- `Assets/Benchmark/Editor`: the runner (menu **Benchmark > Run all**) and 10,000 real Unity `Transform`s as the host API. Writes `results-unity.json`.

### Results

Both guests are built with clang from one source (`bench/guest/guest.c`), the rv64 one with `-ffp-contract=off`. libriscv translation uses `translate_automatic_nbit_address_space`. Milliseconds.

**Native macOS arm64 harness** (`bench/results.json`):

|Case|wamr-interp|wamr-aot|libriscv-interp|libriscv-bt|
|---|---|---|---|---|
|1. Empty export, 1,000,000 host calls|268.973|251.068|6.988|11.884|
|2. 10,000 transforms, AOS|0.475|0.281|0.309|0.113|
|2. 10,000 transforms, SOA|0.135|0.023|0.153|0.043|
|~~3. Load, instantiate, first update, warm~~|~~0.201~~|~~0.110~~|~~0.699~~|~~1.013~~|
|~~3. Same, first ever~~|~~0.373~~|~~0.214~~|~~6.644~~|~~476.322~~|
|4. L-BFGS saddle, 1,000,000 iterations|2862.954|247.112|4066.062|618.835|

**Inside Unity 2022.3.22f1, headless batchmode**, median (min–max) of six runs (`bench/unity-runs/`):

|Case|wamr-interp|wamr-aot|libriscv-interp|libriscv-bt|
|---|---|---|---|---|
|1. 1,000,000 empty calls, loop in C#|289.227 (274.491–300.448)|289.327 (260.014–312.292)|21.302 (14.811–24.447)|27.370 (20.527–37.872)|
|1. Same, loop in native code|273.341 (258.942–281.597)|261.463 (252.173–281.929)|7.232 (6.667–12.121)|12.410 (11.641–13.228)|
|2. 10,000 transforms, AOS|2.987 (2.805–3.483)|2.968 (2.663–3.170)|2.868 (2.552–3.201)|2.633 (2.412–3.050)|
|2. 10,000 transforms, SOA|2.519 (2.446–2.910)|2.498 (2.374–2.714)|2.667 (2.551–2.909)|2.459 (2.317–2.712)|
|~~3. Load, instantiate, first update, warm~~|~~0.218 (0.187–0.312)~~|~~0.154 (0.091–0.166)~~|~~4.540 (3.699–5.200)~~|~~5.649 (5.210–6.203)~~|
|~~3. Same, first in the process~~|~~0.971 (0.876–1.018)~~|~~0.246 (0.144–0.337)~~|~~6.280 (5.311–19.009)~~|~~6.540 (5.807–791.612)~~|
|4. L-BFGS saddle, 1,000,000 iterations|2997.507 (2867.128–3103.850)|261.382 (249.779–279.058)|4012.511 (3867.552–4161.395)|627.478 (611.207–651.306)|

Checks: the transforms sum to 60,000 in both layouts, and the L-BFGS residual sums agree across all four runtimes (1.40992567e-14).

- WAMR AOT is within about 10% of native arm64 clang code on case 4 (223 ms native), scales linearly with the work, and does bounds-check memory (MMU guard pages by default; software checks cost about 9% more).
- libriscv wins the host-boundary cases (1, 2 AOS). WAMR AOT wins compute (case 4), about 2.5x over libriscv's translation and 15x over its interpreter.
- In Unity case 2 is equal for all four runtimes within the spread, because `Transform` access dominates.
- libriscv translation costs 0.5 to 0.9 s the first time and about 1 ms to load once cached. Ship it pre-generated.
- Compile flags (`-O2`, `-mcpu=native`, `-Ofast`) change libriscv case 4 by under 3%.

### libriscv case 4 follow-up

libriscv-bt was 1089 ms and is now about 620 ms. Two harness fixes, no libriscv patch: the guest is built with `-ffp-contract=off` (otherwise fused multiply-adds go through a callback), and the n-bit address space option masks addresses instead of bounds-checking each access. A patch that cached FP registers as C locals gave wrong results (check 5e-315) and was reverted. libriscv-rs (`rvlinux -j`) SIGILLs on macOS arm64 because `icache.rs` reads `ctr_el0`; using `sys_icache_invalidate` on Apple fixes it, and it then runs the 1M-iteration guest in about 1.3 s wall (not measured in this harness, so not in the tables).

What this does not show:

- **Case 3 rows are crossed out (~~like this~~): they load a 5 KB test guest, not the OpenUSD module, so they are invalid for case 3.** The real `usd.elf` (32 MB) runs under libriscv via sandbox-host with Mire and Marocchino: see `bench/usd/results.md` (open takes 3 to 12 s), but it is ~~libriscv only~~ and invalid as a comparison: WAMR cannot run it. WAMR has no equivalent because OpenUSD has no wasm build, so there is no like-for-like USD comparison.
- No gas on either side. One machine.

## Status

Cases 1 to 4 run natively with the test guest, in interpreter and AOT or binary-translation modes. Not built yet: the Unity host for either runtime and the OpenUSD guest.

## Licence

MIT. See [LICENSE](LICENSE).
