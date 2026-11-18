#!/usr/bin/env bash
# Runs cases 1 and 4 in Godot's Sandbox, in a real window (no --headless). GODOT = a double-precision editor with godot-sandbox.
# GD_TIMEOUT=0 for the no-instruction-limit bake. Lines start BTCHECK.
set -euo pipefail
cd "$(dirname "$0")"
: "${GODOT:?set GODOT to the godot double editor binary}"
cp build/bench_guest.elf project/
"$GODOT" --path project --resolution 640x360 --quit-after 2000 2>&1 | grep -E "BTCHECK|ERROR|SCRIPT"
