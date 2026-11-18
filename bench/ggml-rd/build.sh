#!/usr/bin/env bash
# Case 4g: the L-BFGS of case 4 on ggml-rd's kernels, built as a wasm32 reactor and a static riscv64 ELF, with one source,
# one set of flags and standard LLVM cross compilers (wasi-sdk for wasm, clang + lld + the riscv64 sysroot repo for rv64).
set -euo pipefail
cd "$(dirname "$0")"
./fetch.sh
C=$PWD/.cache; GRD=$C/contract-ggml-rd-guest; GG=$C/ggml; SR=$C/repository-riscv64-sysroot/sysroot
W=${WASI_SDK:-$(ls -d ../../.cache/usd-wasm/wasi-sdk-* 2>/dev/null | head -1)}
[ -n "$W" ] && [ -d "$W" ] && W=$(cd "$W" && pwd) || { echo "wasi-sdk missing: run bench/usd-wasm/bootstrap.sh, or set WASI_SDK"; exit 1; }
LLVM=$(brew --prefix llvm)/bin
O=$PWD/out; mkdir -p $O/gen
python3 extract_prelude.py $GRD/kernels/ggml/cpp/add_f32_emit.cpp $O/gen/slang-cpp-prelude.h
python3 $GRD/tests/ggml_rd_kernels/gen_host_kernels.py $GRD/kernels/ggml/kernels.txt $O/gen/host_kernels.cpp $GRD/kernels/ggml/cpp_siblings.txt
printf '#include <float.h>\n#undef FLT16_MIN\n' > $O/gen/nofp16.h
GGC="$GG/src/ggml.c $GG/src/ggml-quants.c"
GGCXX="$GG/src/ggml.cpp $GG/src/ggml-threading.cpp"
OPS=$(ls $GRD/guest/ggml-rd/ops/*.cpp)
OWN="guest_main.cpp lbfgs_rd.cpp $O/gen/host_kernels.cpp $GRD/guest/ggml-rd/rd_pack.cpp $OPS"
INC="-I$GG/include -I$GG/src -I$GRD/guest/ggml-rd -I$GRD/kernels/ggml -I$GRD/kernels/ggml/cpp -I$GRD/tests/ggml_rd_kernels -I$O/gen -I."
DEFS='-DSLANG_ENABLE_BOUND_ZERO_INDEX -DGGML_VERSION="0" -DGGML_COMMIT="0" -DGGML_SCHED_MAX_COPIES=4'
OPT="-O2 -ffp-contract=off -fno-math-errno"   # identical for both targets; -ffp-contract=off keeps the f32 sums bit-equal

build() { # dir cc cxx plat-flags kernel-runner-extra
  local n=$1 cc=$2 cxx=$3 plat=$4 extra=$5 pids=() p s
  rm -rf $O/$n; mkdir -p $O/$n
  for s in $GGC; do $cc $OPT $INC $DEFS $plat -c $s -o $O/$n/c_$(basename $s).o & pids+=($!); done
  for s in $GGCXX $OWN; do
    local x=""; [ "$(basename $s)" = host_kernels.cpp ] && x="$extra"
    $cxx -std=c++20 $OPT -fno-exceptions -fno-rtti $INC $DEFS $plat $x -c $s -o $O/$n/x_$(basename $(dirname $s))_$(basename $s).o & pids+=($!)
  done
  for p in "${pids[@]}"; do wait $p || { echo "compile failed ($n)"; exit 1; }; done
}

# wasm32: a reactor (WAMR runs _initialize, so the op registrars run); wasi-sdk's libc needs WAMR's reference types
WP="--target=wasm32-wasip1 --sysroot=$W/share/wasi-sysroot"
build wasm "$W/bin/clang $WP" "$W/bin/clang++ $WP" "-D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_MMAN" "-D__linux__=1 -include $O/gen/nofp16.h"
$W/bin/clang++ $WP -mexec-model=reactor -Wl,--export=lbfgs -Wl,--gc-sections -Wl,-z,stack-size=1048576 \
  -lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-mman $O/wasm/*.o -o $O/rdbench.wasm
../wamrc-build/wamrc --opt-level=3 -o $O/rdbench.aot $O/rdbench.wasm >/dev/null
../wamrc-build/wamrc --opt-level=3 --bounds-checks=1 -o $O/rdbench.sw.aot $O/rdbench.wasm >/dev/null   # software checks, as a 64-bit memory must use

# wasm64 (memory64): wasi-sdk has no wasm64 sysroot, so this one uses Emscripten's (clang + its wasm64 libc and libc++),
# as a standalone module with no JS runtime. Same sources, same -O2 -ffp-contract=off.
EM="em++ -sMEMORY64=1"
build wasm64 "emcc -sMEMORY64=1" "$EM" "-D_GNU_SOURCE" "-D__linux__=1 -include $O/gen/nofp16.h"
em++ -sMEMORY64=1 -sSTANDALONE_WASM=1 -sWASM_BIGINT=1 -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=268435456 \
  -sSTACK_SIZE=1048576 -sEXPORTED_FUNCTIONS=_lbfgs -sERROR_ON_UNDEFINED_SYMBOLS=1 --no-entry -O2 $O/wasm64/*.o -o $O/rdbench64.wasm
../wamrc-build/wamrc --opt-level=3 -o $O/rdbench64.aot $O/rdbench64.wasm >/dev/null

# riscv64 Linux (glibc sysroot from the sysroot repo; libstdc++ headers sit where clang does not look, see its smoke.py)
GL=$SR/lib/gcc-cross/14
RP="--target=riscv64-unknown-linux-gnu --sysroot=$SR"
build rv64 "$LLVM/clang $RP" "$LLVM/clang++ $RP -isystem $SR/include/c++/14 -isystem $SR/include/c++/14/riscv64-linux-gnu" "-D_GNU_SOURCE" ""
$LLVM/clang++ $RP -fuse-ld=lld -B$GL -L$GL -static -Wl,--gc-sections $O/rv64/*.o -o $O/rdbench.rv64.elf -lm

# the host: WAMR (WASI) and libriscv in one runner
cmake -S host -B $O/host -G Ninja -DDEPS=$PWD/../../deps >/dev/null
cmake --build $O/host
cmake -S host -B $O/host64 -G Ninja -DDEPS=$PWD/../../deps -DWAMR_MEMORY64=ON >/dev/null
cmake --build $O/host64
echo "built: $(ls $O/rdbench64.wasm $O/rdbench64.aot $O/rdbench.wasm $O/rdbench.aot $O/rdbench.rv64.elf $O/host/rdbench | tr '\n' ' ')"
