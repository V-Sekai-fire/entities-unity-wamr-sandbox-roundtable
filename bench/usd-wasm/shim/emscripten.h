// Stand-in for Emscripten's header: OpenUSD's wasm paths call two functions from it.
#pragma once
#include <stdlib.h>
static inline void emscripten_debugger(void) { abort(); }
static inline void emscripten_force_exit(int status) { _Exit(status); }
