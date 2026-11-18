// rdbench <wamr|riscv-interp|riscv-bt> <guest> <solves> [rounds]: case 4g, the L-BFGS over ggml-rd's kernels, in a
// libc guest. wamr takes a wasm reactor (.wasm or its .aot); libriscv a static riscv64 Linux ELF whose main returns,
// after the C runtime ran the op registrars.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" {
#include "wasm_export.h"
}
#include <libriscv/machine.hpp>
#include <libriscv/prepared_call.hpp>

using clk = std::chrono::steady_clock;
static double ms(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

static std::vector<char> slurp(const char *p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(f), {});
}


// Emscripten's wasm64 modules import WASI with 64-bit pointers, which WAMR's built-in WASI (i32 pointers) does not
// provide. The guest only reaches these on a failure path (a message, an abort); an empty environment and no files.
static int32_t w64_fd_write(wasm_exec_env_t env, int32_t fd, int64_t iovs, int64_t n, int64_t pnum) {
  wasm_module_inst_t g_inst = wasm_runtime_get_module_inst(env);
  uint64_t total = 0;
  for (int64_t i = 0; i < n; i++) {
    uint64_t *v = (uint64_t *)wasm_runtime_addr_app_to_native(g_inst, (uint64_t)iovs + 16 * i);
    const void *p = wasm_runtime_addr_app_to_native(g_inst, v[0]);
    if (fd == 1 || fd == 2) fwrite(p, 1, v[1], fd == 1 ? stdout : stderr);
    total += v[1];
  }
  *(uint64_t *)wasm_runtime_addr_app_to_native(g_inst, (uint64_t)pnum) = total;
  return 0;
}
static int32_t w64_fd_close(wasm_exec_env_t, int32_t) { return 0; }
static int32_t w64_environ_sizes_get(wasm_exec_env_t env, int64_t pc, int64_t ps) {
  wasm_module_inst_t g_inst = wasm_runtime_get_module_inst(env);
  *(uint64_t *)wasm_runtime_addr_app_to_native(g_inst, (uint64_t)pc) = 0;
  *(uint64_t *)wasm_runtime_addr_app_to_native(g_inst, (uint64_t)ps) = 0;
  return 0;
}
static int32_t w64_environ_get(wasm_exec_env_t, int64_t, int64_t) { return 0; }
static int32_t w64_fd_seek(wasm_exec_env_t, int32_t, int64_t, int32_t, int64_t) { return 8; }
static NativeSymbol w64_natives[] = {
  {"fd_write", (void *)w64_fd_write, "(iIII)i"},
  {"fd_close", (void *)w64_fd_close, "(i)i"},
  {"environ_sizes_get", (void *)w64_environ_sizes_get, "(II)i"},
  {"environ_get", (void *)w64_environ_get, "(II)i"},
  {"fd_seek", (void *)w64_fd_seek, "(iIiI)i"},
};

static int run_wamr(const char *path, int solves, int rounds) {
  RuntimeInitArgs init;
  memset(&init, 0, sizeof init);
  init.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&init)) return 1;
  std::vector<char> image = slurp(path);
  const bool wasm64 = strstr(path, "64") != nullptr;  // the 64-bit guests are named *64.wasm / *64.aot
  if (wasm64) wasm_runtime_register_natives("wasi_snapshot_preview1", w64_natives, 5);
  char err[256];
  wasm_module_t mod = wasm_runtime_load((uint8_t *)image.data(), image.size(), err, sizeof err);
  if (!mod) { fprintf(stderr, "load: %s\n", err); return 1; }
  if (!wasm64) wasm_runtime_set_wasi_args_ex(mod, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, 0, 1, 2);
  wasm_module_inst_t inst = wasm_runtime_instantiate(mod, 1u << 20, 0, err, sizeof err);
  if (!inst) { fprintf(stderr, "instantiate: %s\n", err); return 1; }
  wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 1u << 20);
  wasm_function_inst_t f = wasm_runtime_lookup_function(inst, "lbfgs");
  if (!f) { fprintf(stderr, "no lbfgs export\n"); return 1; }
  for (int r = 0; r < rounds; r++) {
    uint32_t a[4] = {(uint32_t)solves, 20, 0, 0};
    auto t = clk::now();
    if (!wasm_runtime_call_wasm(env, f, 2, a)) { fprintf(stderr, "call: %s\n", wasm_runtime_get_exception(inst)); return 1; }
    double v; memcpy(&v, a, 8);
    printf("wamr round %d: %.1f ms residual %.9g\n", r, ms(t), v);
  }
  return 0;
}

using M = riscv::Machine<riscv::RISCV64>;
static int run_riscv(const char *path, bool bt, int solves, int rounds) {
  std::vector<char> image = slurp(path);
  riscv::MachineOptions<riscv::RISCV64> o;
  o.memory_max = 256ull << 20;
  o.use_shared_execute_segments = false;
  o.translate_enabled = bt;
  o.translate_invoke_compiler = bt;
  o.translate_ignore_instruction_limit = true;
  o.translation_cache = true;
  o.translation_prefix = "rvbintr-";
  o.translate_automatic_nbit_address_space = bt;
  // RT_BOUNDS: mask (default: the address is masked into the power-of-two arena), check (a check per access), none (unchecked)
  if (const char *e = getenv("RT_BOUNDS")) {
    const std::string b = e;
    if (b == "check") o.translate_automatic_nbit_address_space = false;
    else if (b == "none") { o.translate_automatic_nbit_address_space = false; o.translate_unsafe_remove_checks = true; }
  }
  // the C runtime's startup, and the ggml-rd registrars, are what lives in .init_array; ignore .text so the whole
  // executable is one translated segment (the usd.elf finding: a call into a second section would run untranslated)
  o.ignore_text_section = true;
  try {
    M m(std::string_view(image.data(), image.size()), o);
    m.setup_linux_syscalls();
    m.setup_linux({"rdbench"}, {"LC_ALL=C"});
    m.simulate(2000000000ull);
    if (bt && !m.cpu.current_execute_segment().is_binary_translated()) {
      fprintf(stderr, "binary translation requested but not active\n");
      return 1;
    }
    riscv::PreparedCall<riscv::RISCV64, double(int, int)> lb(m, "lbfgs");
    for (int r = 0; r < rounds; r++) {
      auto t = clk::now();
      double v = lb(solves, 20);
      printf("%s round %d: %.1f ms residual %.9g\n", bt ? "riscv-bt" : "riscv-interp", r, ms(t), v);
    }
  } catch (const std::exception &e) { fprintf(stderr, "libriscv: %s\n", e.what()); return 1; }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: rdbench <wamr|riscv-interp|riscv-bt> <guest> <solves> [rounds]\n"); return 2; }
  const std::string k = argv[1];
  const int solves = atoi(argv[3]), rounds = argc > 4 ? atoi(argv[4]) : 3;
  if (k == "wamr") return run_wamr(argv[2], solves, rounds);
  return run_riscv(argv[2], k == "riscv-bt", solves, rounds);
}
