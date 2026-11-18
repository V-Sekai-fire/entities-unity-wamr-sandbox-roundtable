// ub_bake <guest.rv64.elf> <out dir>: runs libriscv's binary translation once, with the same options the
// plugin uses, and writes the translation as ub-<HASH>.cpp. Compiling that file into the plugin
// (-DUB_EMBED_SRC=...) lets the plugin find the translation by hash with no compiler at run time.
#include "rt.hpp"

#include <fstream>
#include <iterator>
#include <sys/stat.h>

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: ub_bake <guest.rv64.elf> <out dir>\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<char> image((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (image.empty()) { fprintf(stderr, "ub_bake: cannot read %s\n", argv[1]); return 1; }
  mkdir(argv[2], 0755);
  setenv("UB_BAKE_DIR", argv[2], 1);
  RuntimeInitArgs ia; memset(&ia, 0, sizeof ia);
  ia.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&ia)) return 1;
  register_host_api();
  Riscv r(true);
  if (!r.open(image)) { fprintf(stderr, "ub_bake: translation did not engage\n"); return 1; }
  printf("baked into %s\n", argv[2]);
  return 0;
}
