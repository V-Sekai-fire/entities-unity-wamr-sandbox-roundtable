# entities-unity-wamr-sandbox-roundtable

Benchmark comparing WAMR and godot-sandbox as script runtimes inside Unity Engine.

## Cases

1. **Empty call.** Call an exported empty function 1,000,000 times.
2. **Host API transforms.** 10,000 transform reads and writes through the host API.
3. **Cold load.** Load a downloaded module, instantiate it, and run its first update. The module is the OpenUSD guest.
4. **Compute.** 1,000,000 iterations of a complex math problem: L-BFGS (limited-memory BFGS) solving a saddle problem.

## Runtimes

- [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime)
- [godot-sandbox](https://github.com/libriscv/godot-sandbox)

## Guest modules

- [interactor-usd-guest](https://github.com/V-Sekai-fire/interactor-usd-guest)
- [contract-tris-to-quads-guest](https://github.com/V-Sekai-fire/contract-tris-to-quads-guest)

## Tech stack

Taken from `chibifire-stages/character-fox-manifest`, which already runs godot-sandbox guests in Unity.

- `dev.chibifire.sandbox-host` (Unity 2022.3, Editor only): `sandbox_host.dll` wraps libriscv's C API and answers godot-sandbox's host ECALLs natively. The godot-sandbox side of the benchmark drives its `sbhost_*` C ABI (ABI version 2).
- ABI conventions to copy for WAMR: int64 machine ids instead of pointers, a `.sigs` contract that gates the header, the C# P/Invoke file and the DLL exports, a little-endian Variant wire, and resumable gassed calls (`sbhost_resume`, `sbhost_abort`).
- Native build: CMake with llvm-mingw cross-compiling the Windows DLL, environment pinned with Pixi.
- libriscv is pinned at `e121610a`, matching sandbox-host.
- Guests: `interactor-usd-guest` (case 3), `contract-tris-to-quads-guest`.
- Case 4 needs the same instruction budget on both runtimes. Without it, report wall time only.

## Dependencies

Each is a squashed git subtree under `deps/`. Upstream submodule links were removed.

| Path | Upstream | Commit |
|---|---|---|
| `deps/wamr` | bytecodealliance/wasm-micro-runtime | `f298aef0` |
| `deps/godot-sandbox` | libriscv/godot-sandbox | `4f9c73d6` |
| `deps/libriscv` | libriscv/libriscv | `e121610a` |
| `deps/interactor-usd-guest` | V-Sekai-fire/interactor-usd-guest | `fb960c02` |
| `deps/contract-tris-to-quads-guest` | V-Sekai-fire/contract-tris-to-quads-guest | `f61de603` |

## Run

    brew install llvm riscv64-elf-gcc ninja cmake
    bench/guest/build.sh
    cmake -S bench/host -B bench/build -G Ninja && cmake --build bench/build
    bench/build/bench bench/guest/guest.wasm bench/guest/guest.rv64.elf 1

The last argument divides the iteration counts (1 = full size).

## Results

Native macOS arm64 harness, not yet inside Unity. One guest source (`bench/guest/guest.c`) is built for wasm32 and rv64gc. WAMR runs as its fast interpreter (no AOT, no JIT). libriscv runs with its default build options. Raw output is in `bench/results.json`; times are milliseconds.

| Case | WAMR | libriscv |
|---|---|---|
| 1. Empty export, 1,000,000 calls from the host (total) | 283.9 | 31.8 |
| 2. 10,000 transforms, AOS: one host call per transform (median of 200) | 0.466 | 0.394 |
| 2. 10,000 transforms, SOA: 12 column reads and 12 column writes (median of 200) | 0.132 | 0.090 |
| 3. Read module from disk, instantiate, first update (median of 200) | 0.178 | 0.628 |
| 4. L-BFGS saddle, 50,000 solves x 20 iterations = 1,000,000 iterations (total) | 2807.5 | 3110.4 |

Checks that the work was done: both runtimes leave the host transforms summing to 60,000 in cases 2 AOS and 2 SOA. In case 4 the summed residuals agree to 7 digits (1.40992567e-14 and 1.40992534e-14). They are not identical because the rv64 build uses fused multiply-add.

What this does not show:

- Unity is not in the loop. The numbers are the runtimes and the host-call path, not the P/Invoke boundary. Case 1 and the host-call cost in case 2 will change inside Unity.
- Case 3 loads a 5 KB test guest, not the OpenUSD module. The OpenUSD guest is not built here.
- The two runtimes use different host-call paths (WAMR native symbols, libriscv ecall handlers), and the guest ABIs differ in pointer width.
- libriscv's instruction-limit gas is enabled in every call. WAMR runs without gas, so case 4 does not give both runtimes the same budget.
- One machine, one run of the totals. No variance for cases 1 and 4.

## Status

Cases 1 to 4 run natively with the test guest. Not built yet: the Unity host for either runtime, the OpenUSD guest, and AOT or JIT modes of WAMR.

## Licence

MIT. See [LICENSE](LICENSE).
