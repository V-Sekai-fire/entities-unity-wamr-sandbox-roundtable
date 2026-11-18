# ~~Case 3 with the real OpenUSD guest (libriscv only)~~

**Invalid as a comparison.** `usd.elf` runs only on libriscv; WAMR has no counterpart, so there is nothing to compare against. Kept as a libriscv-only measurement. A wasm build of the same core is in progress (`bench/usd-wasm/`).

Guest: `usd.elf` from V-Sekai-fire/interactor-usd-guest release `v20261117-double.1`, 32,200,696 bytes,
sha256 `084710367efc2baaf6710b3c3622863806607840fe131b77618b2b2180e2e2f6`.
Host: `sandbox_host` (chibifire-stages/sandbox-host, private) built for macOS arm64 against `deps/libriscv`,
driven by its probe extended with a `usd` mode: create machine, `usd_init`, push the package in 8 MiB pieces
(the wire caps one array at 16 MiB), `usd_open_staged`, then read counts. Five runs each, fresh machine per run.

| Avatar (package) | create | usd_init | usd_open | meshes / textures |
|---|---|---|---|---|
|~~Mire.usdz (46 MB)~~|~~239–441 ms~~|~~6 ms~~|~~2.9–3.4 s~~|~~12 / 9~~|
|~~Marocchino.usdz (75 MB)~~|~~253–508 ms~~|~~6–10 ms~~|~~5.3–12.1 s (first runs slower, 5.3 s once warm)~~|~~9 / 9~~|

WAMR has no row: there is no wasm build of OpenUSD, and producing one (oneTBB, plugins, the in-memory resolver,
the Variant API) is a port, not a flag. Case 3 against WAMR still uses the 5 KB test guest.
These numbers are the interpreter-or-whatever sandbox-host configures, not a tuned libriscv-bt run.


Superseded by the case 3 section of the README, which adds the WAMR runs of the same core (`bench/usd/wamr-results-first-run.txt`).
