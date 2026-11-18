#include <libriscv/util/crc32.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
using namespace riscv;
static uint32_t H(uint32_t exec, std::map<std::string,std::string> d) {
  std::vector<std::string> v; for (auto &p : d) v.push_back(" -D" + p.first + "=" + p.second);
  std::sort(v.begin(), v.end()); std::string s; for (auto &x : v) s += x;
  return crc32c(exec, s.c_str(), s.size());
}
int main() {
  const uint32_t exec = 0xFB0BF128, target = 0x590873FC;
  std::map<std::string,std::string> base = {{"RISCV_TRANSLATION_DYLIB","8"},{"RISCV_MAX_SYSCALLS","600"},{"RISCV_MACHINE_ALIGNMENT","32"},
    {"RISCV_ARENA_ROEND","1254658"},{"RISCV_EXT_A","1"},{"RISCV_EXT_C","1"}};
  // sanity: my own bake
  auto mine = base; mine["RISCV_ARENA_END"]="268435456"; mine["RISCV_ARENA_OFFSET"]="2368"; mine["RISCV_EXT_VECTOR"]="32"; mine["RISCV_NBIT_AUTOMATIC"]="1"; mine["RISCV_NO_INSTRUCTION_LIMIT"]="1";
  printf("sanity %08X (want FD1FCF38)\n", H(exec, mine));
  long tried = 0;
  for (long arena_end : {268435456L, 268435456L+1048576, 268435456L+(2<<20), 268435456L+(3<<20), 134217728L, 536870912L, 1073741824L})
  for (int vec : {0, 32, 16, 64})
  for (int unchecked = 0; unchecked < 2; unchecked++)
  for (int nbit = 0; nbit < 2; nbit++)
  for (int nolimit = 0; nolimit < 2; nolimit++)
  for (int fcsr = 0; fcsr < 2; fcsr++)
  for (int nan = 0; nan < 2; nan++)
  for (long off = 0; off <= 16384; off += 8) {
    auto d = base; d["RISCV_ARENA_END"] = std::to_string(arena_end); d["RISCV_ARENA_OFFSET"] = std::to_string(off);
    if (vec) d["RISCV_EXT_VECTOR"] = std::to_string(vec);
    if (unchecked) d["RISCV_UNCHECKED_MEMORY"] = "1";
    if (nbit) d["RISCV_NBIT_AUTOMATIC"] = "1";
    if (nolimit) d["RISCV_NO_INSTRUCTION_LIMIT"] = "1";
    if (fcsr) d["RISCV_FCSR"] = "1";
    if (nan) d["RISCV_NANBOXING"] = "1";
    tried++;
    if (H(exec, d) == target) { printf("MATCH arena_end=%ld off=%ld vec=%d unchecked=%d nbit=%d nolimit=%d fcsr=%d nan=%d\n", arena_end, off, vec, unchecked, nbit, nolimit, fcsr, nan); }
  }
  printf("tried %ld\n", tried);
}
