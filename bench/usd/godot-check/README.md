# usd.elf inside Godot's own sandbox

A real-window check (no `--headless`) that loads `usd.elf` into Godot's `Sandbox`, opens a `.usdz` and prints whether
the sandbox reports itself binary translated.

```sh
bench/usd/fetch.sh                      # usd.elf and the avatars into .cache/data
cp .cache/data/usd.elf bench/usd/godot-check/
USDZ=$PWD/.cache/data/Mire.usdz <godot double editor> --path bench/usd/godot-check --quit-after 600
```

Needs a Godot built with godot-sandbox and double precision (usd.elf is a `DOUBLE_PRECISION` build). Lines start `BTCHECK`.
