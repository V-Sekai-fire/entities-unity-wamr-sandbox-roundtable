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

### Results

Native macOS arm64 harness, outside Unity. One guest source (`bench/guest/guest.c`) is built with clang for wasm32 and for rv64gc. Times are milliseconds. Raw output is in `bench/results.json`.

|Case|wamr-interp|wamr-aot|libriscv-interp|libriscv-bt|
|---|---|---|---|---|
|1. Empty export, 1,000,000 host calls (total)|259.7|250.3|6.846|12.6|
|2. 10,000 transforms, AOS, one host call each (median of 200)|0.577|0.401|0.300|0.120|
|2. 10,000 transforms, SOA, 24 column calls (median of 200)|0.132|0.022|0.151|0.051|
|3. Load from disk, instantiate, first update, cache warm (median of 200)|0.175|0.066|0.672|0.905|
|3. Same, first ever (translation compiled and cached)|0.235|0.121|5.806|744.4|
|4. L-BFGS saddle, 1,000,000 iterations (total)|2844.9|253.1|3531.6|1088.5|

Checks: every runtime leaves the host transforms summing to 60,000 in both layouts. The L-BFGS residual sums agree to 7 digits (1.40992567e-14 for WAMR, 1.40992571e-14 for libriscv). They differ because the rv64 build uses fused multiply-add.

Checks on the WAMR AOT numbers:

- Case 4 compiled natively for arm64 with clang `-O2` takes 223 ms (230 to 244 ms over repeated runs) against 253 ms for WAMR AOT, so AOT is within about 10 percent of native code, as expected for AOT.
- AOT time is linear in the work: 278, 126, 64 and 24 ms for 50,000, 25,000, 12,500 and 5,000 solves, and its summed residual equals the interpreter's and the native build's at every size (1.40992567e-14, 7.04962836e-15, 3.52479767e-15, 1.40992567e-15).
- Every one of the 20 iterations of a solve does real work: the residual falls from 1.5 to 2.8e-19 with no stalled iterations.
- WAMR AOT does bounds-check linear memory. On 64-bit hosts `wamrc` leaves the check to the MMU (a guard-page reservation), which costs almost nothing. Compiled with `--bounds-checks=1` (explicit compare-and-branch on every access, which is what libriscv's translation does) case 4 takes 270 ms against 246 ms, and 10,000 AOS transforms take 0.55 ms against 0.41 ms. So the guard pages are worth about 9 percent here; they do not explain the 4x gap to libriscv.
- The case 1 call cost is the same for the interpreter and AOT (about 250 ns per `wasm_runtime_call_wasm`), because the call path, not the guest code, dominates an empty function.

Reading the table:

- libriscv wins the host-boundary cases (1 and 2 AOS). A call is a register setup and a jump; a WAMR call goes through `wasm_runtime_call_wasm`.
- WAMR AOT wins compute (case 4), about 4.3x faster than libriscv's binary translation and 14x faster than its interpreter.
- libriscv binary translation is about 4.5x slower than native code here, not close to native. Its translation, built with libtcc off, keeps guest registers in the CPU structure and checks every load and store against the arena, and its translated code reports `ARCH=HOST_UNKNOWN` on arm64. Turning on register caching and n-bit address masking improves case 4 by at most 20 percent and makes empty calls up to 10 times slower, so they are left at their defaults.
- The guest compiler matters: building the rv64 guest with GCC instead of clang made libriscv 1.5x slower in case 4 (interpreter 5.8 s against 3.5 s, translation 1.7 s against 1.1 s).
- libriscv binary translation costs 745 ms the first time and 0.9 ms to load afterwards. A shipped build needs the translation pre-generated and embedded; libriscv supports embedding the generated source.
- WAMR AOT loads fastest (0.066 ms).

What this does not show:

- Case 3 loads a 5 KB test guest, not the OpenUSD module.
- libriscv is built here as 64-bit only with its default arena. godot-sandbox guests use libriscv's own build options; sandbox-host adds gas and a Variant wire that this harness does not.
- No gas on either side, which sandbox-host needs.
- One machine, one run of the totals in this table. The Unity section has repeated runs.

## Status

Cases 1 to 4 run natively with the test guest, in interpreter and AOT or binary-translation modes. Not built yet: the Unity host for either runtime and the OpenUSD guest.

## Licence

MIT. See [LICENSE](LICENSE).
