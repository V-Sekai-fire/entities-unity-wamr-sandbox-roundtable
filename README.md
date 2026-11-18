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

## Status

Plan only. No benchmark has run yet; no results are recorded.

## Licence

MIT. See [LICENSE](LICENSE).
