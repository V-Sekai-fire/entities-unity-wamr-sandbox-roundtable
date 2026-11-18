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

## Status

Plan only. No benchmark has run yet; no results are recorded.

## Licence

MIT. See [LICENSE](LICENSE).
