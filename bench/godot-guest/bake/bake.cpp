// bake <guest.elf> <out dir> [memory_max_mib]: runs libriscv's binary translation once with the MachineOptions
// godot-sandbox's Sandbox uses, and leaves <out dir>/bintr-<HASH>.dylib, the file Godot looks up by hash.
// The hash covers the execute segment and the generated defines, so it only equals Godot's when the options match.
#include <libriscv/machine.hpp>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
using namespace riscv;
int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: bake <guest.elf> <out dir> [memory_max_mib]\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<char> image((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (image.empty()) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
  const uint64_t mib = argc > 3 ? strtoull(argv[3], nullptr, 10) : 256;
  std::string prefix = std::string(argv[2]) + "/bintr-";
  MachineOptions<RISCV64> o;
  o.memory_max = mib << 20;
  o.stack_size = 2u << 20;                      // Sandbox::GUEST_STACK_SIZE
  o.use_shared_execute_segments = false;
  o.translate_enabled = true;
  o.translate_enable_embedded = true;
  o.translate_future_segments = false;
  o.translate_invoke_compiler = true;           // bake time only: Godot itself never invokes a compiler
  o.translation_cache = true;
  // Godot ignores the limit only when execution_timeout (get_instructions_max) is <= 0; BAKE_NOLIMIT=1 bakes that case.
  o.translate_ignore_instruction_limit = getenv("BAKE_NOLIMIT") != nullptr;
  o.translate_automatic_nbit_address_space = true;  // Godot: m_bintr_automatic_nbit_as defaults to true
  o.translation_prefix = prefix;
  o.translation_suffix = ".dylib";
  if (getenv("BAKE_REGCACHE")) o.translate_use_register_caching = true;  // guest registers (and, with the patch, FP registers) as C locals
  o.translate_unsafe_remove_checks = true;        // Godot: unchecked_memory_wanted() is true here; found by searching the hash (see README)
  o.default_exit_function = "fast_exit";
  o.ignore_text_section = getenv("BAKE_IGNORE_TEXT") != nullptr;
  Machine<RISCV64> m(std::string_view(image.data(), image.size()), o);
  auto &seg = m.memory.exec_segment_for(m.memory.start_address());
  printf("translated=%d hash=%08X exec_crc=%08X\n", int(seg->is_binary_translated()), seg->translation_hash(), seg->crc32c_hash());
  return seg->is_binary_translated() ? 0 : 1;
}
