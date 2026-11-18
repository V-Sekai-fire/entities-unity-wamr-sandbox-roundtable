#!/usr/bin/env bash
# Runs the benchmarks that need no Unity and prints the tables. Run ./build.sh first (and, for case 3,
# bench/usd/fetch.sh and bench/usd-wasm/bootstrap.sh). Writes bench/results.json and bench/usd/wamr-results.txt.
set -euo pipefail
cd "$(dirname "$0")"
G=bench/guest
# libriscv binary translation compiles the guest's translation with the system clang and caches it in the cwd.
mkdir -p bench/work && cd bench/work
echo "== cases 1, 2, 4 (and 3 with the 5 KB test guest, which is not the OpenUSD module)"
CC=${CC:-clang} CFLAGS=${CFLAGS:--O3} ../build/bench ../guest/guest.wasm ../guest/guest.aot ../guest/guest.rv64.elf 1 > ../results.json
cd ../..
python3 bench/table.py bench/results.json

D=.cache/data; U=bench/usd-wasm
if [ -x $U/build/usdbench ] && [ -f $U/out/usd.aot ] && [ -f $D/sample.usdz ]; then
  echo; echo "== case 3: the OpenUSD guest core under WAMR (open = usd_open_staged after the package is copied in), ms"
  : > bench/usd/wamr-results.txt
  for pkg in sample Mire Marocchino; do
    [ -f $D/$pkg.usdz ] || continue
    for img in usd.aot usd.wasm; do
      echo "-- $pkg $img" | tee -a bench/usd/wamr-results.txt
      $U/build/usdbench $U/out/$img $D/$pkg.usdz 5 2>&1 | grep '^run' | sed 's/ msg=.*//' | tee -a bench/usd/wamr-results.txt
    done
  done
else
  echo; echo "case 3 skipped: run bench/usd/fetch.sh and bench/usd-wasm/bootstrap.sh first"
fi
