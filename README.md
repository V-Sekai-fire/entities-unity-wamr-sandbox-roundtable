# entities-unity-wamr-sandbox-roundtable (Godot host)

Benchmark comparing WAMR and godot-sandbox as script runtimes. The Unity host was removed: libriscv runs through godot-sandbox in Godot (`bench/usd/godot-check`), WAMR through the standalone harness.

## Reproduce

Host: macOS on Apple silicon (the Windows DLL is cross-built; the Windows numbers are not measured). Nothing binary is in git; every step below builds from the sources and the pinned `deps/` subtrees.

```sh
brew install llvm llvm@22 lld ninja cmake zig   # zig only for the Windows DLL; Xcode command line tools for clang
git clone https://github.com/V-Sekai-fire/entities-unity-wamr-sandbox-roundtable && cd entities-unity-wamr-sandbox-roundtable
./build.sh                          # wamrc, the guests (wasm, AOT, rv64), the harness
./bench.sh                          # cases 1, 2, 4 ; writes bench/results.json
```

Case 3 with the real OpenUSD module (about 20 minutes for the first build, needs ~10 GB disk in `.cache/`):

```sh
bench/usd/fetch.sh                  # Mire.usdz, Marocchino.usdz, usd.elf: pinned, sha256 checked
bench/usd-wasm/bootstrap.sh         # wasi-sdk, oneTBB, OpenUSD -> usd.wasm, usd.aot, usdbench
./bench.sh                          # now also runs case 3 under WAMR
```

## Cases

1. **Empty call.** Call an exported empty function 1,000,000 times.
2. **Host API transforms.** 10,000 transform reads and writes through the host API.
3. **Cold load.** Load a downloaded module, instantiate it, and run its first update. The module is the OpenUSD guest.
4. **Compute.** 1,000,000 iterations of a complex math problem: L-BFGS (limited-memory BFGS) solving a saddle problem.

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

### libriscv case 4 follow-up

libriscv-bt was 1089 ms and is now about 620 ms. Two harness fixes, no libriscv patch: the guest is built with `-ffp-contract=off` (otherwise fused multiply-adds go through a callback), and the n-bit address space option masks addresses instead of bounds-checking each access. A patch that cached FP registers as C locals gave wrong results (check 5e-315) and was reverted. libriscv-rs (`rvlinux -j`) SIGILLs on macOS arm64 because `icache.rs` reads `ctr_el0`; using `sys_icache_invalidate` on Apple fixes it, and it then runs the 1M-iteration guest in about 1.3 s wall (not measured in this harness, so not in the tables).

What this does not show:

- **Case 3 rows above are crossed out**: they load a 5 KB test guest, not the OpenUSD module. The valid case 3 numbers are in the next section.

### Case 3 with the real OpenUSD module

The same OpenUSD 26.05 core (`usd_core.cpp`, `mem_resolver.cpp` of interactor-usd-guest) built two ways: `usd.elf` for RISC-V (libriscv, via sandbox-host) and `usd.wasm` for wasm32 (WAMR; patches in `bench/usd-wasm/`). Each run is a fresh machine or instance: load, instantiate, `usd_init`, hand over the `.usdz`, open it. Both report the same stage (Mire: 212 prims, 12 meshes, 9 textures; Marocchino: 9 meshes, 9 textures). Milliseconds, macOS arm64.

| | Mire (46 MB) open | Mire total | Marocchino (75 MB) open | Marocchino total |
|---|---|---|---|---|
| WAMR AOT (`usd.aot`), 5 runs | 132–137 | 160–166 | 239–268 | 270–298 |
| WAMR interpreter (`usd.wasm`), 1 run | 1986 | 2205 | 3757 | 3977 |
| libriscv interpreter (`usd.elf`), 5 runs | 2906–3363 | 3264–3657 | 5290–12056 | 5566–12474 |
| libriscv binary translation (`usd.elf`) | not finished | | | |

- "open" is `usd_open_staged` (WAMR) or `usd_push` plus `usd_open_staged` (libriscv). "total" adds machine or instance creation, which is 24 ms for AOT, 210 ms for the WAMR interpreter and 240 to 440 ms for libriscv loading the 32 MB ELF.
- The libriscv rows run inside sandbox-host, which builds libriscv without binary translation, so that row is the interpreter. A translated build (sandbox-host rebuilt with libriscv binary translation on) took 325 s to compile the translation on first start and then opened Mire in the same time as the interpreter. Profiled with `sample`, 0 of 6,715 samples were in the translated library; all were in libriscv's interpreter loop (`simulate_inaccurate`). Lifting libriscv's translation limits did not change that: `translate_blocks_max` (1,024), `translate_instr_max` (500,000), `translate_ignore_instruction_limit`, and the hard-coded `no_mappings > 500000` check in `tr_translate.cpp`, which rejects a full translation after compiling it. With all four lifted and `-O2 -fno-inline` (no cross-function inlining), the 122 MB translated library loaded and was accepted, `open` was still 2975 to 3193 ms, and 0 of 6,715 samples were in it. On the small test guest the same libriscv enters its translated code (L-BFGS 620 ms against 3532 ms interpreted), so the mechanism works; why sandbox-host's calls into `usd.elf` never enter it is not established. The release guest `usd.elf` is built with godot-sandbox's `DOUBLE_PRECISION` (64-bit `real_t`, 40-byte `GuestVariant`), and sandbox-host was written for the 32-bit layout (24-byte `GuestVariant`). I rebuilt sandbox-host with `DOUBLE_PRECISION_REAL_T`, mirroring godot-sandbox's `guest_datatypes.h` (only NIL, BOOL, INT and FLOAT inline; vectors scoped; packed vector arrays and `GuestVariant` sized by `real_t`). Same stage counts, `open` 2936 to 3110 ms, and again 0 of 6,661 samples in the translated library, so the layout mismatch was not the cause. The interpreter-only rows above were measured with the 32-bit host; the calls they make (byte arrays, integers, strings) carry no `real_t`, and I did not re-measure them with the double host. WAMR AOT is about 22x faster than the libriscv interpreter, and no translated libriscv number exists for this guest.
- The libriscv rows are not reproducible from this repository (sandbox-host is private). The WAMR rows are: `bench/usd/fetch.sh`, `bench/usd-wasm/bootstrap.sh`, `./bench.sh`.
- `usd.wasm` is built without C++ exceptions and threads (WAMR has no wasm exception handling); `usd.elf` is also single-threaded.
- No gas on either side. One machine.

## Status

Cases 1 to 4 run natively with the test guest, in interpreter and AOT or binary-translation modes. Not built yet: the Unity host for either runtime and the OpenUSD guest.

## Licence

MIT. See [LICENSE](LICENSE).


## usd.elf with binary translation actually engaged (macOS arm64, Mire)

Cause of the earlier "translation never runs" result: `usd.elf` keeps OpenUSD's static constructors in a `.pxrctor` section after `.text`, inside the same executable segment. libriscv by default makes the initial execute segment `.text` only, so the first call into `.pxrctor` made it build a second, untranslated segment, and everything after ran there (11 entries to the interpreter loop, all on the untranslated segment).

Fix: `MachineOptions::ignore_text_section = true`, so the whole executable PT_LOAD (0x5402CC, length 0xB6B80C) is the segment. The translation (544,344 mappings, all applied) is then entered 28,120,349 times.

| Mire, `open` | ms |
|---|---|
| libriscv interpreter | 2906 to 3363 |
| libriscv binary translation, `.pxrctor` inside the segment | 660 to 665 (4 runs) |
| WAMR AOT | 130 to 139 |
| WAMR interpreter | 1984 to 2077 |

Translation is about 4.5x faster than the libriscv interpreter and about 5x slower than WAMR AOT here. Costs and caveats: the one-time translation compile took 1183 s with `-O2 -fno-inline -fno-inline-functions` (the `-O3` compile exceeded 13 GB of RAM); later runs create in 118 to 178 ms from the cached library. It needed the translation caps lifted (`translate_blocks_max`, `translate_instr_max`, the hard-coded 500,000-mapping check in `tr_translate.cpp`) and `translate_ignore_instruction_limit`, which removes runaway protection. Those changes were made in a scratch copy of libriscv, not in `deps/`, and sandbox-host is a private repo, so this row is not reproducible from this repo alone. Host used the double-precision layout. Marocchino was not re-run.


## Host

The Unity project and plugin (with the baked-translation plugin build) were deleted; the earlier Unity numbers are in git history (up to commit `ec501c7`). Godot's own `Sandbox` is the libriscv host now. Note that godot-sandbox only looks up a prebaked `bintr-<hash>` translation (`sandbox/binary_translation/enabled`, `cache_dir`) and never compiles one, so a Godot run interprets until a translation is baked.


## Cases 1 and 4 through Godot's Sandbox (interpreter)

`bench/godot-guest` builds the benchmark guest as a godot-sandbox program (`./build.sh`: zig, riscv64-linux-musl, double-precision API, llvm-ar). Its L-BFGS is the same source file the wasm and rv64 guests use (`bench/guest/lbfgs.h`). The program carries the `.sandbox_variant` marker (`SBXV`, 40) that the editor requires, as `contract-tris-to-quads-guest` does. Run in a real window (no `--headless`) on the Godot 4.8-beta double editor from `entities-zone`, M2 Pro:

| | Godot Sandbox, interpreter |
|---|---|
| Case 1, 1,000,000 `vmcall("empty")` from a GDScript loop | 65 ms (includes the GDScript loop) |
| Case 4, 1M L-BFGS iterations | 3431 to 3448 ms, residual sum 1.4e-14 (same as the other runtimes) |

For comparison, the standalone libriscv interpreter takes 3.9 to 4.0 s on case 4. Case 1 here is not comparable with the harness row, which loops in native code. Case 2 (transform access) is not ported: it was a Unity `Transform` measurement.

### Binary translation in Godot

This editor has no JIT (`has_feature_jit()` is false), and godot-sandbox never compiles a translation: it loads a prebaked `bintr-<hash>.dylib` from `sandbox/binary_translation/cache_dir`, and only when `sandbox/binary_translation/enabled` is set. `bench/godot-guest/bake.sh` bakes that file with libriscv at godot-sandbox's own pin (`ec5b426b`) and the `MachineOptions` the `Sandbox` builds its machine with; `run.sh` runs the cases in a real window.

The hash has to match, or Godot never loads the file. It is the execute-segment checksum mixed with the generated defines, so I recovered Godot's options by searching for the defines that reproduce the hash the editor asks for (`bake/find_defines.cpp`, 1.8 million candidates, one match for `590873FC`): a 256 MiB arena, arena offset 2368, vector extension 32, **`RISCV_UNCHECKED_MEMORY` on** (Godot builds its machine with `translate_unsafe_remove_checks`), and instruction counting on (off when `execution_timeout` is 0, which is a second hash, `C4AD5EFE`, baked with `BAKE_NOLIMIT=1`). My first bake had both of those the wrong way round, which is why its hashes never matched.

| Case 4, 1M L-BFGS iterations, Godot Sandbox | ms |
|---|---|
| interpreter | 3431 to 3448 |
| binary translation, instruction counting on (`CFLAGS=-O2`) | 1077 to 1100 |
| binary translation, no limit (`execution_timeout = 0`), `-O3 -fno-inline` | 972 |

`is_binary_translated()` is true in both, and the residual sum is 1.4e-14, as in every other runtime. This is slower than the standalone harness (libriscv-bt about 620 ms); the compile flags do not explain it (`-O2`, `-O3`, with and without `-fno-inline` all land near 1.0 s), so the difference is in the guest build (zig, musl, `-O3`, vector extension) or the editor's libriscv, and I have not isolated it. Two caveats: the translated code calls back into the editor's libriscv, which is built from a commit I cannot see (it reproduces the layout of the pin, offset 2368, but that is not proof), and the unchecked-memory translation removes bounds checks that WAMR keeps, so it is not a like-for-like row against WAMR.

`usd.elf` is not translated in Godot: the `Sandbox` leaves `ignore_text_section` off, so the initial segment is `.text` only and the first call into `.pxrctor` switches to an untranslated segment (see above). That needs `.pxrctor` linked into `.text` in the guest, or the option exposed in godot-sandbox.


## Why WAMR AOT is faster than libriscv binary translation (case 4, profiled)

Case 4 on one runtime only (`bench/build/prof_lbfgs wamr|riscv-interp|riscv-bt <guest> [rounds]`), sampled with macOS `sample`, M2 Pro. Same L-BFGS source, same residual sum (1.40992567e-14).

| | ms per 1M iterations |
|---|---|
| wamr-aot | 240 to 244 |
| libriscv-bt, defaults of this harness | 585 to 623 |
| libriscv-bt, `translate_use_register_caching` | 569 to 630 (about 570) |
| libriscv-bt, register caching and `translate_block_split=100000` | 548 to 580 |

**Where the time goes.** 100% of WAMR's samples and 98% of libriscv's (1314 of 1346) are inside generated code. Neither runtime's call, dispatch or syscall path matters here, so the difference is the quality of the machine code. The guest executes 2.71 billion RISC-V instructions for the 1M iterations.

**What the code looks like** (`llvm-objdump` of the AOT object and of the translated library):

| | libriscv-bt translated code | WAMR AOT `lbfgs` function |
|---|---|---|
| instructions per floating-point operation | 13.2 | 2.7 |
| loads and stores | 2787 of 5355 | 472 of 1518 |
| accesses to the guest register file (`[x19/x20, #off]`) | 1547 | 24 |
| guest address masks (`and ..., #0x3ffffff`) | 524 | 0 |
| integer to FP moves (`fmov d, x`) | 176 | 6 |

- **WAMR:** LLVM sees the whole function at `-O3`. It unrolls the N=16 loops, keeps the vectors in `d0`..`d30`, uses paired loads (`ldp`) and does a multiply and an add per element with no stores. Linear memory is one base register, with a guard region instead of a per-access check.
- **libriscv:** each guest instruction is translated on its own. A floating-point value is loaded into an integer register, spilled to the register file, moved with `fmov`, computed, stored to the register file again and reloaded. The hottest instruction in the profile (213 samples) is a `ldr` that reloads a value stored one instruction earlier (`stp d1, d0, [x20, #0x180]` then `ldr x15, [x20, #0x180]`), a store-to-load round trip. Every guest memory access costs an `and` to mask the address and, because the compiler cannot prove the register-file stores leave the arena pointer alone, a reload of the arena base (`ldr x17, [x20, #0x400]`).
- **Register caching** (guest registers as C locals) is not enabled by default in the library, although `docs/INTEGRATION.md` says it is (the code default is off without libtcc). It helps about 5 to 8% here and does not touch the floating-point registers, which is where this guest spends its time. Larger translated blocks help a few percent more. Neither closes the gap, so the harness defaults are unchanged and the table above is the evidence.

So the 2.4x is not bounds checking, call overhead or translation coverage. It is that a translator working instruction by instruction, with the guest state in memory, cannot match a compiler that optimises the whole function. A fix would have to keep the FP registers in C locals as well.
