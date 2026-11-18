#!/usr/bin/env bash
# Case 4g sources, pinned, cloned into bench/ggml-rd/.cache (not tracked: the riscv64 sysroot alone is 149 MB).
# Needs read access to the three V-Sekai-fire repos (public) and the wasi-sdk that bench/usd-wasm/bootstrap.sh fetches.
set -euo pipefail
cd "$(dirname "$0")"
C=.cache; mkdir -p $C
pin() { # name url sha
  if [ ! -d $C/$1/.git ]; then git init -q $C/$1 && git -C $C/$1 remote add origin "$2"; fi
  if [ "$(git -C $C/$1 rev-parse HEAD 2>/dev/null || true)" != "$3" ]; then
    git -C $C/$1 fetch -q --depth 1 origin "$3" && git -C $C/$1 checkout -q FETCH_HEAD
  fi
  echo "$1 $(git -C $C/$1 rev-parse --short HEAD)"
}
pin contract-ggml-rd-guest https://github.com/V-Sekai-fire/contract-ggml-rd-guest.git 94b6be4d3dabf50fcde08315b90033c65e054d8f
pin ggml                   https://github.com/V-Sekai-fire/ggml.git                   29cae86726d8ae2b4d699d8e432fd1bb1801e959
pin repository-riscv64-sysroot https://github.com/V-Sekai-fire/repository-riscv64-sysroot.git 585de9e0cfe15fc14f9961083d857d609592de42
