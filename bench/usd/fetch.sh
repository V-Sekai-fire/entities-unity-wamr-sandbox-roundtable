#!/usr/bin/env bash
# Downloads the case 3 inputs into .cache/data: the two avatars (public repos, pinned commits, sizes and sha256
# checked) and the OpenUSD RISC-V guest usd.elf (release asset, sha256 checked), plus a one-triangle sample.usdz.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); D=$(cd "$HERE/../.." && pwd)/.cache/data
mkdir -p "$D"
get() { # file url sha256
  if [ -f "$D/$1" ] && [ "$(shasum -a 256 "$D/$1" | cut -d' ' -f1)" = "$3" ]; then return; fi
  curl -fL "$2" -o "$D/$1.part"; mv "$D/$1.part" "$D/$1"
  [ "$(shasum -a 256 "$D/$1" | cut -d' ' -f1)" = "$3" ] || { echo "sha256 mismatch for $1" >&2; exit 1; }
}
get Mire.usdz       https://github.com/chibifire-stages/character-mille-mire-feuille/raw/ee77c5b1198b441bf261fe27e85dd2f86e1781ff/usd/Mire.usdz       df30af479f46f4ae9885fb04b476f95e06674dd09854c55b2d6a8af2738d87e7
get Marocchino.usdz https://github.com/chibifire-stages/character-marocchino/raw/dcd38ff7f6f2ea10a584dca383a6b7c3fab27d36/usd/Marocchino.usdz         07a3db8cf477bd6feeb9853ffb252368604d6d0061497111a82ce1e9a9636ec3
get usd.elf         https://github.com/V-Sekai-fire/interactor-usd-guest/releases/download/v20261117-double.1/usd.elf                                  084710367efc2baaf6710b3c3622863806607840fe131b77618b2b2180e2e2f6
if [ ! -f "$D/sample.usdz" ]; then
  T=$(mktemp -d); cp "$HERE/sample.usda" "$T/"; (cd "$T" && zip -0 -q sample.usdz sample.usda); mv "$T/sample.usdz" "$D/"
fi
ls -la "$D"
